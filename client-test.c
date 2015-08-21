#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define BUFF_SIZE 1024
#define RDMA_MAX_HEAD 16
#define POLL_WC_SIZE 128
#define REG_PER_CONN 128

/***************************************************************************//**
 * Testing parameters
 *
 ******************************************************************************/
static char     *pstr_server = "127.0.0.1";
static char     *pstr_port = "11211";
static int      thread_number = 1;
static int      request_number = 10000;
static int      last_time = 1000;    /* secs */
static int      verbose = 0;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 8;

/***************************************************************************//**
 * Testing message
 *
 ******************************************************************************/
static char add_noreply[] = "add foo 0 0 1 noreply\r\n1\r\n";
static char set_noreply[] = "set foo 0 0 1 noreply\r\n1\r\n";
static char replace_noreply[] = "replace foo 0 0 1 noreply\r\n1\r\n";
static char append_noreply[] = "append foo 0 0 1 noreply\r\n1\r\n";
static char prepend_noreply[] = "prepend foo 0 0 1 noreply\r\n1\r\n";
static char incr_noreply[] = "incr foo 1 noreply\r\n";
static char decr_noreply[] = "decr foo 1 noreply\r\n";
static char delete_noreply[] = "delete foo noreply\r\n";

static char add_reply[] = "add foo 0 0 1\r\n1\r\n";
static char set_reply[] = "set foo 0 0 1\r\n1\r\n";
static char replace_reply[] = "replace foo 0 0 1\r\n1\r\n";
static char append_reply[] = "append foo 0 0 1\r\n1\r\n";
static char prepend_reply[] = "prepend foo 0 0 1\r\n1\r\n";
static char incr_reply[] = "incr foo 1\r\n";
static char decr_reply[] = "decr foo 1\r\n";
static char delete_reply[] = "delete foo\r\n";

/***************************************************************************//**
 * Relative resources around connection
 *
 ******************************************************************************/
struct rdma_context {
    struct ibv_context          **device_ctx_list;
    struct ibv_context          *device_ctx;
    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_cq               *send_cq;
    struct ibv_cq               *recv_cq;

    struct rdma_event_channel   *cm_channel;
    
    struct rdma_cm_id           *listen_id;

} rdma_ctx;

struct wr_context;

struct rdma_conn {
    struct rdma_cm_id   *id;

    struct ibv_pd       *pd;
    struct ibv_cq       *send_cq;
    struct ibv_cq       *recv_cq;

    char                *rbuf;
    struct ibv_mr       *rmr;

    struct ibv_mr           **rmr_list;
    char                    **rbuf_list;
    struct wr_context       *wr_ctx_list;
    size_t                  rsize; 
    size_t                  buff_list_size;
};

struct wr_context {
    struct rdma_conn       *c;
    struct ibv_mr           *mr;
};

/***************************************************************************//**
 * Description 
 * Init rdma global resources
 *
 ******************************************************************************/
int
init_rdma_global_resources() {
    memset(&rdma_ctx, 0, sizeof(struct rdma_context));

    int num_device;
    if ( !(rdma_ctx.device_ctx_list = rdma_get_devices(&num_device)) ) {
        perror("rdma_get_devices()");
        return -1;
    }
    rdma_ctx.device_ctx = *rdma_ctx.device_ctx_list;
    printf("Get device: %d\n", num_device); 

    /*
    if ( !(rdma_ctx.comp_channel = ibv_create_comp_channel(rdma_ctx.device_ctx)) ) {
        perror("ibv_create_comp_channel");
        return -1;
    }
    */

    if ( !(rdma_ctx.pd = ibv_alloc_pd(rdma_ctx.device_ctx)) ) {
        perror("ibv_alloc_pd");
        return -1;
    }

    if ( !(rdma_ctx.send_cq = ibv_create_cq(rdma_ctx.device_ctx, 
                    cq_size, NULL, NULL, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    if ( !(rdma_ctx.recv_cq = ibv_create_cq(rdma_ctx.device_ctx, 
                    cq_size, NULL, NULL, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

     return 0;
}

/***************************************************************************//**
 * Connection server
 *
 ******************************************************************************/
static struct rdma_conn *
build_connection() {
    struct rdma_conn        *c = calloc(1, sizeof(struct rdma_conn));
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
    qp_attr.send_cq = rdma_ctx.send_cq;
    qp_attr.recv_cq = rdma_ctx.recv_cq;

    if (0 != rdma_create_qp(c->id, rdma_ctx.pd, &qp_attr)) {
        perror("rdma_create_qp()");
        return NULL;;
    }

    if (0 != rdma_connect(c->id, NULL)) {
        perror("rdma_connect()");
        return NULL;
    }

    c->buff_list_size = REG_PER_CONN;
    c->rsize = BUFF_SIZE;
    c->rbuf_list = calloc(c->buff_list_size, sizeof(char*));
    c->rmr_list = calloc(c->buff_list_size, sizeof(struct ibv_mr*));
    c->wr_ctx_list = calloc(c->buff_list_size, sizeof(struct wr_context));

    int i = 0;
    for (i = 0; i < c->buff_list_size; ++i) {
        c->rbuf_list[i] = malloc(c->rsize);
        c->rmr_list[i] = rdma_reg_msgs(c->id, c->rbuf_list[i], c->rsize);
        c->wr_ctx_list[i].c = c;
        c->wr_ctx_list[i].mr = c->rmr_list[i];
        if (0 != rdma_post_recv(c->id, &c->wr_ctx_list[i], c->rmr_list[i]->addr, c->rmr_list[i]->length, c->rmr_list[i])) {
            perror("rdma_post_recv()");
            return NULL;
        }
    }

    return c;
}

/***************************************************************************//**
 * send mr
 ******************************************************************************/
int 
send_mr(struct rdma_cm_id *id, struct ibv_mr *mr) {
    if (0 != rdma_post_send(id, id->context, mr->addr, mr->length, mr, 0)) {
        perror("rdma_post_send()");
        return -1;
    }

    struct ibv_wc wc;
    int cqe = 0;
    do {
        cqe = ibv_poll_cq(rdma_ctx.send_cq, 1, &wc);
    } while (cqe == 0);

    if (cqe < 0) {
        return -1;
    } else {
        return 0;
    }
}

/***************************************************************************//**
 * Receive message bt RDMA recv operation
 *
 ******************************************************************************/
static int
recv_msg(struct rdma_conn *c) {
    struct ibv_wc wc;
    int cqe = 0;
    do {
        cqe = ibv_poll_cq(rdma_ctx.recv_cq, 1, &wc);
    } while (cqe == 0);

    if (cqe < 0) {
        return -1;
    }
    
    struct wr_context *wr_ctx = (struct wr_context*)(uintptr_t)wc.wr_id;
    struct ibv_mr *mr = wr_ctx->mr;
    
    if (0 != rdma_post_recv(c->id, wr_ctx, mr->addr, mr->length, mr)) {
        perror("rdma_post_recv()");
        return -1;
    }

    return 0;
}

/***************************************************************************//**
 * Test command with registered memory
 *
 ******************************************************************************/
void *
test_with_regmem(void *arg) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection()) ) {
        return NULL;
    }

    printf("noreply:\n");

    struct ibv_mr   *add_noreply_mr = rdma_reg_msgs(c->id, add_noreply, sizeof(add_noreply));
    struct ibv_mr   *set_noreply_mr = rdma_reg_msgs(c->id, set_noreply, sizeof(set_noreply));
    struct ibv_mr   *replace_noreply_mr = rdma_reg_msgs(c->id, replace_noreply, sizeof(replace_noreply));
    struct ibv_mr   *append_noreply_mr = rdma_reg_msgs(c->id, append_noreply, sizeof(append_noreply));
    struct ibv_mr   *prepend_noreply_mr = rdma_reg_msgs(c->id, prepend_noreply, sizeof(prepend_noreply));
    struct ibv_mr   *incr_noreply_mr = rdma_reg_msgs(c->id, incr_noreply, sizeof(incr_noreply));
    struct ibv_mr   *decr_noreply_mr = rdma_reg_msgs(c->id, decr_noreply, sizeof(decr_noreply));
    struct ibv_mr   *delete_noreply_mr = rdma_reg_msgs(c->id, delete_noreply, sizeof(delete_noreply));

    for (i = 0; i < request_number; ++i) {
        send_mr(c->id, add_noreply_mr);
        send_mr(c->id, set_noreply_mr);
        send_mr(c->id, replace_noreply_mr);
        send_mr(c->id, append_noreply_mr);
        send_mr(c->id, prepend_noreply_mr);
        send_mr(c->id, incr_noreply_mr);
        send_mr(c->id, decr_noreply_mr);
        send_mr(c->id, delete_noreply_mr);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

    printf("\nreply:\n");
    clock_gettime(CLOCK_REALTIME, &start);

    struct ibv_mr   *add_reply_mr = rdma_reg_msgs(c->id, add_reply, sizeof(add_reply));
    struct ibv_mr   *set_reply_mr = rdma_reg_msgs(c->id, set_reply, sizeof(set_reply));
    struct ibv_mr   *replace_reply_mr = rdma_reg_msgs(c->id, replace_reply, sizeof(replace_reply));
    struct ibv_mr   *append_reply_mr = rdma_reg_msgs(c->id, append_reply, sizeof(append_reply));
    struct ibv_mr   *prepend_reply_mr = rdma_reg_msgs(c->id, prepend_reply, sizeof(prepend_reply));
    struct ibv_mr   *incr_reply_mr = rdma_reg_msgs(c->id, incr_reply, sizeof(incr_reply));
    struct ibv_mr   *decr_reply_mr = rdma_reg_msgs(c->id, decr_reply, sizeof(decr_reply));
    struct ibv_mr   *delete_reply_mr = rdma_reg_msgs(c->id, delete_reply, sizeof(delete_reply));

    for (i = 0; i < request_number; ++i) {
        send_mr(c->id, add_reply_mr);
        recv_msg(c);
        send_mr(c->id, set_reply_mr);
        recv_msg(c);
        send_mr(c->id, replace_reply_mr);
        recv_msg(c);
        send_mr(c->id, append_reply_mr);
        recv_msg(c);
        send_mr(c->id, prepend_reply_mr);
        recv_msg(c);
        send_mr(c->id, incr_reply_mr);
        recv_msg(c);
        send_mr(c->id, decr_reply_mr);
        recv_msg(c);
        send_mr(c->id, delete_reply_mr);
        recv_msg(c);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
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
            "c:"    /* thread number */
            "r:"    /* request number per thread */
            "t:"    /* last time, secs */
            "p:"    /* listening port */
            "s:"    /* server ip */
            "R"     /* whether receive message from server */
            "v"     /* verbose */
    ))) {
        switch (c) {
            case 'c':
                thread_number = atoi(optarg);
                break;
            case 'r':
                request_number = atoi(optarg);
                break;
            case 't':
                last_time = atoi(optarg);
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
            default:
                assert(0);
        }
    }

    init_rdma_global_resources();

    struct timespec start,
                    finish;
    clock_gettime(CLOCK_REALTIME, &start);

    test_with_regmem(NULL);

    clock_gettime(CLOCK_REALTIME, &finish);

    printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    return 0;
}

