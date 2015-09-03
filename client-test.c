#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define BUFF_SIZE 4096

/***************************************************************************//**
 * Testing parameters
 *
 ******************************************************************************/
static char     *pstr_server = "127.0.0.1";
static char     *pstr_port = "11211";
static int      thread_number = 1;
static int      request_number = 10000;
static int      verbose = 0;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 16;
static int      buff_per_conn = 4;
static int      poll_wc_size = 128;

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
static char get_reply[] = "get foo\r\n";
static char delete_reply[] = "delete foo\r\n";

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

    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *listen_id;
    
    int                         thread_id;
};

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
static struct thread_context*
init_rdma_thread_resources() {
    struct thread_context *ctx = calloc(1, sizeof(struct thread_context));

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
        perror("ibv_alloc_pd");
        return NULL;
    }

    if ( !(ctx->send_cq = ibv_create_cq(ctx->device_ctx, 
                    cq_size, NULL, NULL, 0)) ) {
        perror("ibv_create_cq");
        return NULL;
    }

    if ( !(ctx->recv_cq = ibv_create_cq(ctx->device_ctx, 
                    cq_size, NULL, NULL, 0)) ) {
        perror("ibv_create_cq");
        return NULL;
    }

    return ctx;
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

    c->buff_list_size = buff_per_conn;
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
static int 
send_mr(struct rdma_cm_id *id, struct ibv_mr *mr) {
    if (0 != rdma_post_send(id, NULL, mr->addr, mr->length, mr, 0)) {
        perror("rdma_post_send()");
        return -1;
    }

    struct ibv_wc wc;
    int cqe = 0;
    do {
        cqe = ibv_poll_cq(id->send_cq, 1, &wc);
    } while (cqe == 0);
    
    if (cqe < 0) {
        return -1;
    } else {
        if (IBV_WC_SUCCESS != wc.status) {
            printf("BAD WC [%d]\n", wc.status);
            if (0 != rdma_post_send(id, NULL, mr->addr, mr->length, mr, 0)) {
                perror("rdma_post_send()");
                return -1;
            }

            struct ibv_wc wc;
            int cqe = 0;
            do {
                cqe = ibv_poll_cq(id->send_cq, 1, &wc);
            } while (cqe == 0);
            printf("THE NEW WC [%d]\n", wc.status);
        }
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
        cqe = ibv_poll_cq(c->id->recv_cq, 1, &wc);
    } while (cqe == 0);

    if (cqe < 0) {
        return -1;
    }
    
    struct wr_context *wr_ctx = (struct wr_context*)(uintptr_t)wc.wr_id;
    struct ibv_mr *mr = wr_ctx->mr;

    if (verbose) {
        printf("CLIENT RECV, length %d:\n%s\n", wc.byte_len, (char*)mr->addr);
    }
    
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
static char kValue[] = "VALUE ";
static void
test_with_regmem(struct thread_context *ctx) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection(ctx)) ) {
        return;
    }

    printf("[%d] noreply:\n", ctx->thread_id);

    struct ibv_mr   *mr = rdma_reg_msgs(c->id, kValue, 6);
    if (!mr) {
        perror("rdma_reg_msgs()");
    }

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
    printf("[%d] Cost time: %lf secs\n", ctx->thread_id, 
        (double)(finish.tv_sec-start.tv_sec + (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

    printf("[%d] reply:\n", ctx->thread_id);
    clock_gettime(CLOCK_REALTIME, &start);

    struct ibv_mr   *add_reply_mr = rdma_reg_msgs(c->id, add_reply, sizeof(add_reply));
    struct ibv_mr   *set_reply_mr = rdma_reg_msgs(c->id, set_reply, sizeof(set_reply));
    struct ibv_mr   *replace_reply_mr = rdma_reg_msgs(c->id, replace_reply, sizeof(replace_reply));
    struct ibv_mr   *append_reply_mr = rdma_reg_msgs(c->id, append_reply, sizeof(append_reply));
    struct ibv_mr   *prepend_reply_mr = rdma_reg_msgs(c->id, prepend_reply, sizeof(prepend_reply));
    struct ibv_mr   *incr_reply_mr = rdma_reg_msgs(c->id, incr_reply, sizeof(incr_reply));
    struct ibv_mr   *decr_reply_mr = rdma_reg_msgs(c->id, decr_reply, sizeof(decr_reply));
    struct ibv_mr   *get_reply_mr = rdma_reg_msgs(c->id, get_reply, sizeof(get_reply));
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
        send_mr(c->id, get_reply_mr);
        recv_msg(c);
        send_mr(c->id, delete_reply_mr);
        recv_msg(c);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("[%d] Cost time: %lf secs\n", ctx->thread_id, 
            (double)(finish.tv_sec-start.tv_sec + (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
}

/***************************************************************************//**
 * Test large memory
 *
 ******************************************************************************/
static char large_add_noreply[10240] = "add foo 0 0 1 noreply\r\n1\r\n";
static char large_add_reply[10240] = "add foo 0 0 1\r\n1\r\n";

static void 
test_large_memory(struct thread_context *ctx) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection(ctx)) ) {
        return;
    }

    printf("[%d] large, noreply:\n", ctx->thread_id);

    struct ibv_mr *add_noreply_mr = rdma_reg_msgs(c->id, large_add_noreply, sizeof(large_add_noreply));
    for (i = 0; i < request_number; ++i) {
        send_mr(c->id, add_noreply_mr);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("[%d] Cost time: %lf secs\n", ctx->thread_id, 
        (double)(finish.tv_sec-start.tv_sec + (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

    return;

    printf("[%d] reply:\n", ctx->thread_id);
    clock_gettime(CLOCK_REALTIME, &start);

    struct ibv_mr *add_reply_mr = rdma_reg_msgs(c->id, large_add_reply, sizeof(large_add_reply));
    for (i = 0; i < request_number; ++i) {
        send_mr(c->id, add_reply_mr);
        recv_msg(c);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("[%d] Cost time: %lf secs\n", ctx->thread_id, 
            (double)(finish.tv_sec-start.tv_sec + (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
}

/***************************************************************************//**
 *  
 ******************************************************************************/
static void
test_max_conns(struct thread_context *ctx) {
    max_sge = 1;
    wr_size = 1;
    buff_per_conn = 1;
    int i = 0; 

    struct rdma_conn *c = NULL;
    for (i = 0; i < request_number; ++i) {
        if ( !(c = build_connection(ctx)) ) {
            break;
        }
    }
    printf("send %d conns\n", i);
}

/***************************************************************************//**
 * split and send large momery
 *
 ******************************************************************************/
#define SPLIT_SIZE 16384
static char split_add_reply[32768] = "add foo 0 0 20000\r\n???";
static char split_delete_reply[32768] = "delete foo\r\n";

static void
split_and_send_large_memory(struct rdma_conn *c, char *mem, size_t length) {
    size_t rlen = length;
    char *p = mem;

    struct ibv_mr *mr = NULL;
    size_t mem_len = 0;

    while (rlen > 0) {
        mem_len = rlen < SPLIT_SIZE ? rlen : SPLIT_SIZE;
        mr = rdma_reg_msgs(c->id, p, mem_len);
        rlen -= mem_len;

        send_mr(c->id, mr);
        rdma_dereg_mr(mr);
        
        p += mem_len;
    }
}

/***************************************************************************//**
 * recv split messages
 *
 ******************************************************************************/
static char *
recv_split_msgs(struct rdma_conn *c, size_t size, size_t *getlen) {
    size_t buff_num = (float)size / SPLIT_SIZE + 0.5;
    char *buff = malloc(buff_num * size);
    char *curr = buff;

    struct ibv_wc wc;
    int cqe = 0;
    int i = 0;

    for (i = 0; i < buff_num; ++i) {
        do {
            cqe = ibv_poll_cq(c->id->recv_cq, 1, &wc);
        } while (cqe == 0);

        if (cqe < 0) {
            free(buff);
            *getlen = 0;
            return NULL;
        }

        struct wr_context *wr_ctx = (struct wr_context*)(uintptr_t)wc.wr_id;
        struct ibv_mr *mr = wr_ctx->mr;

        if (verbose) {
            printf("CLIENT RECV, length %d:\n%s\n", wc.byte_len, (char*)mr->addr);
        }
        
        memcpy(curr, mr->addr, wc.byte_len);
        curr += wc.byte_len;

        if (wc.byte_len >= 5 && 0 == strncmp(mr->addr + wc.byte_len - 5, "END\r\n", 5)) {
            break;
        }
    }
    
    *getlen += curr - buff;
    return buff;
}

/***************************************************************************//**
 * testing
 *
 ******************************************************************************/
static void
test_split_memory(struct thread_context *ctx) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection(ctx)) ) {
        return;
    }

    struct ibv_mr   *get_reply_mr = rdma_reg_msgs(c->id, get_reply, sizeof(get_reply));
    struct ibv_mr   *delete_reply_mr = rdma_reg_msgs(c->id, delete_reply, sizeof(delete_reply));

    printf("[%d] split, noreply:\n", ctx->thread_id);

    split_add_reply[20019] = '\r';
    split_add_reply[20020] = '\n';
    for (i = 0; i < request_number; ++i) {
        split_and_send_large_memory(c, split_add_reply, 32768);
        recv_msg(c);
        send_mr(c->id, get_reply_mr);
        size_t len = 0;
        recv_split_msgs(c, 32768, &len);
        send_mr(c->id, delete_reply_mr);
        recv_msg(c);
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("[%d] Cost time: %lf secs\n", ctx->thread_id, 
        (double)(finish.tv_sec-start.tv_sec + (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
}

/***************************************************************************//**
 *  
 ******************************************************************************/
#define LARGE_SIZE 100000
#define HEAD_SIZE 32
static char large_buff[LARGE_SIZE] = "this is large buff\n";
static char head_buff[HEAD_SIZE];

void
test_send_read_request(struct rdma_conn *c) {
    /* test normal buff */
    struct ibv_mr   *add_reply_mr = rdma_reg_msgs(c->id, add_reply, sizeof(add_reply));
    send_mr(c->id, add_reply_mr);
    recv_msg(c);

    /* test large buff */
    struct ibv_mr *head_mr = rdma_reg_msgs(c->id, head_buff, HEAD_SIZE);
    struct ibv_mr *large_mr = rdma_reg_read(c->id, large_buff, LARGE_SIZE);

    snprintf(head_buff, HEAD_SIZE, "%c %lu %u %zu\n",'\x88', (uint64_t)(uintptr_t)large_mr->addr, large_mr->rkey, large_mr->length);
    send_mr(c->id, head_mr);
    recv_msg(c);
}

void
test_rdma_read(struct thread_context *ctx) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection(ctx)) ) {
        return;
    }

    test_send_read_request(c);
}

/***************************************************************************//**
 * thread run
 *
 ******************************************************************************/
void *
thread_run(void *arg) {
    int thread_id = (int)arg;
    struct thread_context *ctx = init_rdma_thread_resources();
    if (!ctx) {
        return NULL;
    }

    ctx->thread_id = thread_id;
    //test_with_regmem(ctx);
    //test_large_memory(ctx);
    //test_split_memory(ctx);
    test_rdma_read(ctx);
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
                buff_per_conn = atoi(optarg);
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
        thread_run(NULL);

    } else {
        int i = 0;
        for (i = 0; i < thread_number; ++i) {
            printf("Thread %d\n begin\n", i);

            if (0 != pthread_create(threads+i, NULL, thread_run, (void*)i)) {
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

