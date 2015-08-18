#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

/***************************************************************************//**
 * Settings
 *
 ******************************************************************************/
#define MAX_BUFF_SIZE 1024

static int      backlog = 1024;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 8;
static char     *port = "5555";
static int      request_num = 1000;
static int      verbose = 0;

struct rdma_context {
    struct ibv_context      **device_ctx_list;
    struct ibv_context      *device_ctx;
    struct ibv_pd           *pd;
    struct ibv_cq           *cq;
    
    struct rdma_cm_id       *listen_id;
    struct rdma_cm_id       *id;
} rdma_context;

struct rdma_conn {
    struct ibv_mr           *smr;
    char                    *sbuf;
    size_t                  ssize;

    struct ibv_mr           *rmr_list;
    char                    *rbuf_list;
    size_t                  rsize; 
} rdma_conn;


/***************************************************************************//**
 * Listen rdma connection request, only for one connection
 * 
 ******************************************************************************/
int accpet_connection() {
    /* init connection resources */
    memset(&rdma_context, 0, sizeof(struct rdma_context));
    int device_num = 0;
    if ( !(rdma_context.device_ctx_list = rdma_get_devices(&device_num)) ) {
        perror("rdma_get_devices():");
        return -1;
    }
    printf("Get rdma device context: %d\n", device_num);
    rdma_context.device_ctx = rdma_context.device_ctx_list[0];

    if ( !(rdma_context.pd = ibv_alloc_pd(rdma_context.device_ctx)) ) {
        perror("ibv_alloc_pd()");
        return -1;
    }

    if ( !(rdma_context.cq = ibv_create_cq(rdma_context.device_ctx, cq_size, NULL, NULL, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    /* bind address */
    struct rdma_addrinfo    hints,
                            *res = NULL;
    memset(&hints, 0, sizeof(struct rdma_addrinfo));

    hints.ai_flags = RAI_PASSIVE;
    hints.ai_port_space = RDMA_PS_TCP;
    if (0 != rdma_getaddrinfo(NULL, port, &hints, &res)) {
        perror("rdma_addrinfo()");
        return -1;
    }

    /* create qp */
    struct ibv_qp_init_attr qp_attr;
    memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));
	qp_attr.cap.max_send_wr = 8;
	qp_attr.cap.max_recv_wr = wr_size;
	qp_attr.cap.max_send_sge = max_sge;
	qp_attr.cap.max_recv_sge = 1;
	qp_attr.sq_sig_all = 1;
	qp_attr.qp_type = IBV_QPT_RC;
	//qp_attr.send_cq = rdma_context.cq;
	//qp_attr.recv_cq = rdma_context.cq;

    if (0 != rdma_create_ep(&rdma_context.listen_id, res, NULL, &qp_attr)) {
        perror("rdma_create_ep()");
    }

    /* listen */
    if (0 != rdma_listen(rdma_context.listen_id, backlog)) {
        perror("rdma_listen()");
        return -1;
    }

    printf("Listening on port %d\n", ntohs(rdma_get_src_port(rdma_context.listen_id)) );

    if (0 != rdma_get_request(rdma_context.listen_id, &rdma_context.id)) {
        perror("rdma_get_request()");
        return -1;
    }

    /*
    if (0 != rdma_create_qp(rdma_context.id, rdma_context.pd, &qp_attr)) {
        perror("rdma_create_qp()");
        return -1;
    }
    */

    if (0 != rdma_accept(rdma_context.id, NULL)) {
        perror("rdma_accept()");
        return -1;
    }

    printf("recv cq:%p\n", (void*)rdma_context.id->recv_cq);
    //rdma_context.id->recv_cq = rdma_context.cq;

    memset(&rdma_conn, 0, sizeof(struct rdma_conn));
    rdma_conn.ssize = rdma_conn.rsize = MAX_BUFF_SIZE;
    rdma_conn.sbuf = malloc(rdma_conn.ssize);

    if ( !(rdma_conn.smr = rdma_reg_msgs(rdma_context.id, rdma_conn.sbuf, rdma_conn.ssize)) ) {
        perror("rdma_reg_msgs()");
        return -1;
    }

    return 0;
}

/***************************************************************************//**
 * 
 ******************************************************************************/
void test_one_recv() {

//    char *one_recv = malloc(MAX_BUFF_SIZE);
    char *one_recv = calloc(1, MAX_BUFF_SIZE);
    struct ibv_mr *recv_mr = rdma_reg_msgs(rdma_context.id, one_recv, MAX_BUFF_SIZE);

    int count = 0;
    struct ibv_wc wc;
    int cqe = 0;

    while (request_num--) {
        if ('*' == one_recv[0]) {
            printf("error\n");
        }
        one_recv[0] = '*';
        if (0 != rdma_post_recv(rdma_context.id, NULL, recv_mr->addr, recv_mr->length, recv_mr)) {
            perror("rdma_post_recv()");
            break;
        }

        /*
        do {
            cqe = ibv_poll_cq(rdma_context.cq, 1, &wc);
            if (cqe < 0) {
                perror("ibv_poll_cq()");
                return;
            }
            if (IBV_WC_SUCCESS != wc.status) {
                printf("Get bad wc!\n");
                return;
            }
        } while (cqe == 0);
        */

        
        cqe = rdma_get_recv_comp(rdma_context.id, &wc);
        if (cqe < 0) {
            perror("rdma_get_recv_comp()");
            break;
        }
        

        if (cqe == 0) {
            printf("cqe 0\n");
        }

        count += 1;
        if (count % 500 == 0) {
            printf("RECV %d: %s\n", count, one_recv);
        }
    }
}

int main(int argc, char *argv[]) {
    char c;
    while (-1 != (c = getopt(argc, argv,
            "r:"    /* request number per thread */
            "p:"    /* listening port */
            "v"     /* verbose */
    ))) {
        switch (c) {
            case 'r':
                request_num = atoi(optarg);
                break;
            case 'p':
                port = optarg;
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                assert(0);
        }
    }

    if (0 != accpet_connection()) return -1;
    test_one_recv();
    return 0;
}
