#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <event.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include "hashtable.h"

#define BUFF_SIZE 4096

/***************************************************************************//**
 * Relative resources around connection
 *
 ******************************************************************************/
struct thread_context {
    struct ibv_context          **device_ctx_list;
    struct ibv_context          *device_ctx;
    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_srq              *srq;
    struct ibv_cq               *send_cq;
    struct ibv_cq               *recv_cq;

    struct rdma_cm_id           *listen_id;

    struct event_base           *base;
    struct event                poll_event;

    hashtable_t                 *qp_hash;

    size_t                      rsize;
    size_t                      buff_list_size;     
    char                        **rbuf_list;
    struct ibv_mr               **rmr_list;

    struct ibv_wc               *poll_wc;
    size_t                      ack_events;

    int                         thread_id;
};

struct rdma_conn {
    struct rdma_cm_id   *id;

    size_t  total_recv;

    void (*handle_recv) (struct ibv_wc*, struct rdma_conn*);
    void (*handle_send) (struct ibv_wc*, struct rdma_conn*);
    void (*handle_read) (struct ibv_wc*, struct rdma_conn*);
    void (*handle_write) (struct ibv_wc*, struct rdma_conn*);

    void *context;
};

/***************************************************************************//**
 * prototype
 *
 ******************************************************************************/
void * thread_run(void *arg);
void cc_poll_event_handler(int fd, short libevent_event, void *arg);

static struct thread_context* init_rdma_thread_resources();
static int init_and_dispatch_event(struct thread_context *ctx);
static struct rdma_conn* build_connection(struct thread_context *ctx);
static void handle_work_complete(struct ibv_wc *wc, struct rdma_conn *c);

static int send_mr(struct rdma_cm_id *id, struct ibv_mr *mr);

static void test_with_regmem(struct thread_context *ctx);

/***************************************************************************//**
 * Testing parameters
 *
 ******************************************************************************/
static char     *pstr_server = "127.0.0.1";
static char     *pstr_port = "11211";
static int      thread_number = 1;
static int      request_number = 10000;
static int      verbose = 0;
static int      srq_size = 1024;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 16;
static int      buff_per_thread = 128;
static int      poll_wc_size = 128;
static size_t   ack_events = 16;

/***************************************************************************//**
 * Testing message
 *
 ******************************************************************************/

static char add_reply[] = "add foo 0 0 1\r\n1\r\n";
static char set_reply[] = "set foo 0 0 1\r\n1\r\n";
static char replace_reply[] = "replace foo 0 0 1\r\n1\r\n";
static char append_reply[] = "append foo 0 0 1\r\n1\r\n";
static char prepend_reply[] = "prepend foo 0 0 1\r\n1\r\n";
static char incr_reply[] = "incr foo 1\r\n";
static char decr_reply[] = "decr foo 1\r\n";
static char get_reply[] = "get foo\r\n";
static char delete_reply[] = "delete foo\r\n";

/*
struct ibv_mr   *add_reply_mr = NULL;
struct ibv_mr   *set_reply_mr = NULL;
struct ibv_mr   *replace_reply_mr = NULL;
struct ibv_mr   *append_reply_mr = NULL;
struct ibv_mr   *prepend_reply_mr = NULL;
struct ibv_mr   *incr_reply_mr = NULL;
struct ibv_mr   *decr_reply_mr = NULL;
struct ibv_mr   *get_reply_mr = NULL;
struct ibv_mr   *delete_reply_mr = NULL;
*/

/***************************************************************************//**
 * Description 
 * Init rdma global resources
 *
 ******************************************************************************/
static struct thread_context*
init_rdma_thread_resources() {

    struct thread_context *ctx = calloc(1, sizeof(struct thread_context));

    ctx->qp_hash = hashtable_create(1024);

    int num_device;
    if ( !(ctx->device_ctx_list = rdma_get_devices(&num_device)) ) {
        perror("rdma_get_devices()");
        return NULL;
    }
    ctx->device_ctx = *ctx->device_ctx_list;
    if (verbose) {
        printf("Get device: %d\n", num_device); 
    }

    if ( !(ctx->pd = ibv_alloc_pd(ctx->device_ctx)) ) {
        perror("ibv_alloc_pd()");
        return NULL;
    }

    if ( !(ctx->comp_channel = ibv_create_comp_channel(ctx->device_ctx)) ) {
        perror("ibv_create_comp_channel()");
        return NULL;
    }

    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.srq_context = NULL;
    srq_init_attr.attr.max_sge = 16;
    srq_init_attr.attr.max_wr = srq_size;
    srq_init_attr.attr.srq_limit = srq_size; /* RDMA TODO: what is srq_limit? */

    if ( !(ctx->srq = ibv_create_srq(ctx->pd, &srq_init_attr)) ) {
        perror("ibv_create_srq()");
        return NULL;
    }

    if ( !(ctx->send_cq = ibv_create_cq(ctx->device_ctx, 
                    cq_size, NULL, ctx->comp_channel, 0)) ) {
        perror("ibv_create_cq()");
        return NULL;
    }

    if (0 != ibv_req_notify_cq(ctx->send_cq, 0)) {
        perror("ibv_reg_notify_cq()");
        return NULL;
    }

    if ( !(ctx->recv_cq = ibv_create_cq(ctx->device_ctx, 
                    cq_size, NULL, ctx->comp_channel, 0)) ) {
        perror("ibv_create_cq()");
        return NULL;
    }

    if (0 != ibv_req_notify_cq(ctx->recv_cq, 0)) {
        perror("ibv_reg_notify_cq()");
        return NULL;
    }

    ctx->rsize = BUFF_SIZE;
    ctx->rbuf_list = calloc(buff_per_thread, sizeof(char *));
    ctx->rmr_list = calloc(buff_per_thread, sizeof(struct ibv_mr*));
    ctx->poll_wc = calloc(poll_wc_size, sizeof(struct ibv_wc));

    int i = 0;
    for (i = 0; i < buff_per_thread; ++i) {
        ctx->rbuf_list[i] = malloc(ctx->rsize);
        if (ctx->rbuf_list[i] == 0) {
            break;
        }
    }
    if (i != buff_per_thread) {
        int j = 0;
        for (j = 0; j < i; ++j) {
            free(ctx->rbuf_list[j]);
        }
        free(ctx->rbuf_list);
        ctx->rbuf_list = 0;
    }
    if (!ctx->rmr_list || !ctx->rbuf_list) {
        fprintf(stderr, "out of ctxmory in init_rdma_thread_resources()\n");
        return NULL;
    }

    struct ibv_recv_wr *bad = NULL;
    struct ibv_sge sge;
    struct ibv_recv_wr rwr;
    for (i = 0; i < buff_per_thread; ++i) {
        ctx->rmr_list[i] = ibv_reg_mr(ctx->pd, ctx->rbuf_list[i], ctx->rsize, IBV_ACCESS_LOCAL_WRITE);

        sge.addr = (uintptr_t)ctx->rbuf_list[i];
        sge.length = ctx->rsize;
        sge.lkey = ctx->rmr_list[i]->lkey;

        rwr.wr_id = (uintptr_t)ctx->rmr_list[i];
        rwr.next = NULL;
        rwr.sg_list = &sge;
        rwr.num_sge = 1;

        if (0 != ibv_post_srq_recv(ctx->srq, &rwr, &bad)) {
            perror("ibv_post_srq_recv()");
            return NULL;
        }
    }

    return ctx;
}

/***************************************************************************//**
 *  
 ******************************************************************************/
static int init_and_dispatch_event(struct thread_context *ctx) {
    ctx->base = event_base_new();
    event_set(&ctx->poll_event, ctx->comp_channel->fd, EV_READ | EV_PERSIST,
            cc_poll_event_handler, ctx);
    event_base_set(ctx->base, &ctx->poll_event);

    if (event_add(&ctx->poll_event, 0) == -1) {
        perror("event_add()");
        return -1;
    }
    event_base_dispatch(ctx->base);

    return 0;
}

/***************************************************************************//**
 * Connection server
 *
 ******************************************************************************/
static struct rdma_conn *
build_connection(struct thread_context *ctx) {
    struct rdma_conn *c = calloc(1, sizeof(struct rdma_conn));

    if (0 != rdma_create_id(NULL, &c->id, c, RDMA_PS_TCP)) {
        perror("rdma_create_id()");
        return NULL;
    }
    struct rdma_addrinfo    hints = { .ai_port_space = RDMA_PS_TCP },
                            *res = NULL;
    if (0 != rdma_getaddrinfo(pstr_server, pstr_port, &hints, &res)) {
        perror("rdma_getaddrinfo()");
        return NULL;
    }

    int ret = 0;
    ret = rdma_resolve_addr(c->id, NULL, res->ai_dst_addr, 100);  // wait for 100 ms
    ret = rdma_resolve_route(c->id, 100); 

    rdma_freeaddrinfo(res);
    if (0 != ret) {
        perror("Error on resolving addr or route");
        return NULL;
    }


    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));
    qp_attr.cap.max_send_wr = 8;
    qp_attr.cap.max_recv_wr = wr_size;
    qp_attr.cap.max_send_sge = max_sge;
    qp_attr.cap.max_recv_sge = max_sge;
    qp_attr.sq_sig_all = 1;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.send_cq = ctx->send_cq;
    qp_attr.recv_cq = ctx->recv_cq;
    qp_attr.srq = ctx->srq;

    if (0 != rdma_create_qp(c->id, ctx->pd, &qp_attr)) {
        perror("rdma_create_qp()");
        return NULL;;
    }

    if (0 != rdma_connect(c->id, NULL)) {
        perror("rdma_connect()");
        return NULL;
    }

    c->id->recv_cq = ctx->recv_cq;
    c->id->send_cq = ctx->send_cq;
    c->id->srq = ctx->srq;

    if (0 != hashtable_insert(ctx->qp_hash, c->id->qp->qp_num, c)) {
        fprintf(stderr, "hashtable insert error!\n");
        return NULL;
    }

    return c;
}

/***************************************************************************//**
 * send mr
 ******************************************************************************/
static int 
send_mr(struct rdma_cm_id *id, struct ibv_mr *mr) {
    return rdma_post_send(id, mr, mr->addr, mr->length, mr, 0);
}

/***************************************************************************//**
 *  
 ******************************************************************************/
void 
cc_poll_event_handler(int fd, short libevent_event, void *arg) {
    struct thread_context *ctx = arg;

    struct ibv_cq *cq = NULL;
    void *null = NULL;
    if (0 != ibv_get_cq_event(ctx->comp_channel, &cq, &null)) {
        perror("ibv_get_cq_event()");
        return;
    }

    if (++(ctx->ack_events) == ack_events) {
        ibv_ack_cq_events(cq, ctx->ack_events);
        ctx->ack_events = 0;
    }
    if (0 != ibv_req_notify_cq(cq, 0)) {
        perror("ibv_reg_notify_cq()");
        return;
    }

    /* poll complete queue */
    int cqe = 0, i = 0;
    struct rdma_conn *c = NULL;
    do {
        if ( (cqe = ibv_poll_cq(cq, poll_wc_size, ctx->poll_wc)) < 0) {
            perror("ibv_poll_cq()");
            return;
        }

        for (i = 0; i < cqe; ++i) {
            c = hashtable_search(ctx->qp_hash, ctx->poll_wc[i].qp_num);
            handle_work_complete(ctx->poll_wc + i, c);
        }
    } while (cqe == poll_wc_size);

}

/***************************************************************************//**
 *  
 ******************************************************************************/
static void 
handle_work_complete(struct ibv_wc *wc, struct rdma_conn *c) {
    if (IBV_WC_SUCCESS != wc->status) {
        fprintf(stderr, "BAD WC [%d]\n", (int)wc->status);
        rdma_disconnect(c->id);
        return;
    }

    if (IBV_WC_RECV & wc->opcode) {
        c->handle_recv(wc, c);
        return;
    }

    switch (wc->opcode) {
        case IBV_WC_SEND:
            c->handle_send(wc, c);
            break;
        case IBV_WC_RDMA_WRITE:
            c->handle_write(wc, c);
            break;
        case IBV_WC_RDMA_READ:
            c->handle_read(wc, c);
            break;
        default:
            fprintf(stderr, "unhandled event [%d]\n", (int)wc->opcode);
            break;
    }
}

/***************************************************************************//**
 * Test with regmem
 *
 ******************************************************************************/

struct test_regmem_context {
    struct ibv_mr *mr[9];
    size_t  index;
};

void handle_recv_regmem(struct ibv_wc *wc, struct rdma_conn *c) {
    if (c->total_recv == request_number) {
        exit(0);
        return;
    }

    struct ibv_mr *mr = (struct ibv_mr*)(uintptr_t)wc->wr_id;
    struct test_regmem_context *regmem_ctx = c->context;

    if (verbose) {
        fprintf(stderr, "RECV, length: %d:\n%s\n", wc->byte_len, (char*)mr->addr);
    }

    if ( ++(regmem_ctx->index) == 9) {
        regmem_ctx->index = 0;
    }

    if (0 != rdma_post_recv(c->id, mr, mr->addr, mr->length, mr)) {
        perror("rdma_post_recv()");
        return;
    }

    send_mr(c->id, regmem_ctx->mr[regmem_ctx->index]);

    c->total_recv += 1;
    if (c->total_recv % 10000 == 0) {
        fprintf(stderr, "RECV, length: %d:\n%s\n", wc->byte_len, (char*)mr->addr);
    }
}

void handle_send_regmem(struct ibv_wc *wc, struct rdma_conn *c) {
    struct ibv_mr *mr = (struct ibv_mr*)(uintptr_t)wc->wr_id;
    if (verbose) {
        fprintf(stderr, "SEND, length: %d:\n%s\n", mr->length, (char*)mr->addr);
    }
}

static void 
test_with_regmem(struct thread_context *ctx) {
    struct rdma_conn *c = NULL;
    struct timespec start, finish;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection(ctx)) ) {
        return;
    }

    struct test_regmem_context *regmem_ctx = calloc(1, sizeof(struct test_regmem_context));
    c->context = regmem_ctx;
    c->handle_recv = handle_recv_regmem;
    c->handle_send = handle_send_regmem;

    regmem_ctx->mr[0] = rdma_reg_msgs(c->id, add_reply, sizeof(add_reply));
    regmem_ctx->mr[1] = rdma_reg_msgs(c->id, set_reply, sizeof(set_reply));
    regmem_ctx->mr[2] = rdma_reg_msgs(c->id, replace_reply, sizeof(replace_reply));
    regmem_ctx->mr[3] = rdma_reg_msgs(c->id, append_reply, sizeof(append_reply));
    regmem_ctx->mr[4] = rdma_reg_msgs(c->id, prepend_reply, sizeof(prepend_reply));
    regmem_ctx->mr[5] = rdma_reg_msgs(c->id, incr_reply, sizeof(incr_reply));
    regmem_ctx->mr[6] = rdma_reg_msgs(c->id, decr_reply, sizeof(decr_reply));
    //regmem_ctx->mr[7] = rdma_reg_msgs(c->id, get_reply, sizeof(get_reply));
    regmem_ctx->mr[7] = rdma_reg_msgs(c->id, decr_reply, sizeof(decr_reply));
    regmem_ctx->mr[8] = rdma_reg_msgs(c->id, delete_reply, sizeof(delete_reply));
    
    send_mr(c->id, regmem_ctx->mr[0]);

    init_and_dispatch_event(ctx);

    clock_gettime(CLOCK_REALTIME, &finish);
}

/***************************************************************************//**
 * thread run
 *
 ******************************************************************************/
void *
thread_run(void *arg) {

    struct thread_context *ctx = init_rdma_thread_resources();
    if (!ctx) {
        return NULL;
    }
    ctx->thread_id = *((int*)arg);
    free(arg);

    test_with_regmem(ctx);

    return NULL;
}

/***************************************************************************//**
 * main
 *
 ******************************************************************************/
int 
main(int argc, char *argv[]) {
    char        c = '\0';
    while (-1 != (c = getopt(argc, argv,
            "t:"    /* thread number */
            "r:"    /* request number per thread */
            "p:"    /* listening port */
            "s:"    /* server ip */
            "v"     /* verbose */
            "b:"
    ))) {
        switch (c) {
            case 't':
                thread_number = atoi(optarg);
                break;
            case 'r':
                request_number = atoi(optarg);
                break;
            case 'p':
                pstr_port = optarg;
                break;
            case 's':
                pstr_server = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'b':
                buff_per_thread = atoi(optarg);
                break;
            case 'w':
                poll_wc_size = atoi(optarg);
            default:
                assert(0);
        }
    }

    struct timespec start,
                    finish;
    clock_gettime(CLOCK_REALTIME, &start);


    pthread_t *threads = calloc(thread_number, sizeof(pthread_t));

    if (1 == thread_number) {
        /* use main thread by default */
        int *thread_id = malloc(sizeof(int));
        *thread_id = 0;
        thread_run(thread_id);

    } else {
        int i = 0;
        for (i = 0; i < thread_number; ++i) {
            printf("Thread %d\n begin\n", i);

            int *thread_id = malloc(sizeof(int));
            *thread_id = i;
            if (0 != pthread_create(threads+i, NULL, thread_run, thread_id)) {
                return -1;
            }
        }

        for (i = 0; i < thread_number; ++i) {
            pthread_join(threads[i], NULL);
            printf("Thread %d terminated.\n", i);
        }
    }

    clock_gettime(CLOCK_REALTIME, &finish);

    printf("MAIN Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    return 0;
}

