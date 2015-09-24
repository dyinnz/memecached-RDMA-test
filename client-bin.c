#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include "protocol_binary.h"
#include "build_cmd.h"

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
test_with_regmem(int if_binary) {
    struct rdma_conn *c = NULL;
    struct timespec start,
                    finish;
    int i = 0;

    clock_gettime(CLOCK_REALTIME, &start);
    if ( !(c = build_connection()) ) {
        return NULL;
    }

    if (if_binary == 1) {
	struct ibv_mr 	*get_mr = 	rdma_reg_msgs( 	c->id, 	get_bin, 	request_size);
	struct ibv_mr   *add_mr = 	rdma_reg_msgs( 	c->id, 	add_bin, 	request_size);
	struct ibv_mr   *set_mr = 	rdma_reg_msgs( 	c->id, 	set_bin, 	request_size);
	struct ibv_mr   *replace_mr = 	rdma_reg_msgs( 	c->id, 	replace_bin, 	request_size);
	struct ibv_mr   *append_mr = 	rdma_reg_msgs( 	c->id, 	append_bin, 	request_size);
	struct ibv_mr   *prepend_mr = 	rdma_reg_msgs( 	c->id, 	prepend_bin, 	request_size);
	struct ibv_mr   *incr_mr = 	rdma_reg_msgs( 	c->id, 	incr_bin, 	request_size);
	struct ibv_mr   *decr_mr = 	rdma_reg_msgs( 	c->id, 	decr_bin, 	request_size);
	struct ibv_mr   *delete_mr = 	rdma_reg_msgs( 	c->id, 	delete_bin, 	request_size);

	printf("\nbinary:\n");

	clock_gettime(CLOCK_REALTIME, &start);
	
	for (i = 0; i < request_number; ++i) {
	    send_mr(c->id, get_mr);
	    recv_msg(c);
	    send_mr(c->id, add_mr);
	    recv_msg(c);
	    send_mr(c->id, set_mr);
	    recv_msg(c);
	    send_mr(c->id, replace_mr);
	    recv_msg(c);
	    send_mr(c->id, append_mr);
	    recv_msg(c);
	    send_mr(c->id, prepend_mr);
	    recv_msg(c);
	    send_mr(c->id, incr_mr);
	    recv_msg(c);
	    send_mr(c->id, decr_mr);
	    recv_msg(c);
	    send_mr(c->id, delete_mr);
	    recv_msg(c);
	}

	printf("Binary cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec +
	            (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    } else {
	printf("\nascii:\n");
	struct ibv_mr 	*get_nr_mr 	= 	rdma_reg_msgs( c->id, 	get_ascii_noreply, 	request_size);
	struct ibv_mr   *add_nr_mr 	= 	rdma_reg_msgs( c->id, 	add_ascii_noreply, 	request_size);
	struct ibv_mr   *set_nr_mr 	= 	rdma_reg_msgs( c->id, 	set_ascii_noreply, 	request_size);
	struct ibv_mr   *replace_nr_mr 	= 	rdma_reg_msgs( c->id, 	replace_ascii_noreply, 	request_size);
	struct ibv_mr   *append_nr_mr 	= 	rdma_reg_msgs( c->id, 	append_ascii_noreply, 	request_size);
	struct ibv_mr   *prepend_nr_mr 	= 	rdma_reg_msgs( c->id, 	prepend_ascii_noreply, 	request_size);
	struct ibv_mr   *incr_nr_mr 	= 	rdma_reg_msgs( c->id, 	incr_ascii_noreply, 	request_size);
	struct ibv_mr   *decr_nr_mr 	= 	rdma_reg_msgs( c->id, 	decr_ascii_noreply, 	request_size);
	struct ibv_mr   *delete_nr_mr 	= 	rdma_reg_msgs( c->id, 	delete_ascii_noreply, 	request_size);

	struct ibv_mr   *get_r_mr 	=       rdma_reg_msgs( c->id, 	get_ascii_reply, 	request_size);
	struct ibv_mr   *add_r_mr 	=       rdma_reg_msgs( c->id, 	add_ascii_reply, 	request_size);
	struct ibv_mr   *set_r_mr 	=       rdma_reg_msgs( c->id, 	set_ascii_reply, 	request_size);
	struct ibv_mr   *replace_r_mr 	=       rdma_reg_msgs( c->id, 	replace_ascii_reply, 	request_size);
	struct ibv_mr   *append_r_mr 	=       rdma_reg_msgs( c->id, 	prepend_ascii_reply, 	request_size);
	struct ibv_mr   *prepend_r_mr 	=       rdma_reg_msgs( c->id, 	append_ascii_reply, 	request_size);
	struct ibv_mr   *incr_r_mr 	=       rdma_reg_msgs( c->id, 	incr_ascii_reply, 	request_size);
	struct ibv_mr   *decr_r_mr 	=       rdma_reg_msgs( c->id, 	decr_ascii_reply, 	request_size);
	struct ibv_mr   *delete_r_mr 	=       rdma_reg_msgs( c->id, 	delete_ascii_reply, 	request_size);

	printf("\tnoreply:\n");

	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < request_number; i++) {
	    send_mr(c->id, get_nr_mr);
	    send_mr(c->id, add_nr_mr);
	    send_mr(c->id, set_nr_mr);
	    send_mr(c->id, replace_nr_mr);
	    send_mr(c->id, append_nr_mr);
	    send_mr(c->id, prepend_nr_mr);
	    send_mr(c->id, incr_nr_mr);
	    send_mr(c->id, decr_nr_mr);
	    send_mr(c->id, delete_nr_mr);
	}
	printf("Ascii noreply cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec +
	            (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

	printf("\treply:\n");

	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < request_number; i++) {
	    send_mr(c->id, get_r_mr);
	    recv_msg(c);
	    send_mr(c->id, add_r_mr);
	    recv_msg(c);
	    send_mr(c->id, set_r_mr);
	    recv_msg(c);
	    send_mr(c->id, replace_r_mr);
	    recv_msg(c);
	    send_mr(c->id, append_r_mr);
	    recv_msg(c);
	    send_mr(c->id, prepend_r_mr);
	    recv_msg(c);
	    send_mr(c->id, incr_r_mr);
	    recv_msg(c);
	    send_mr(c->id, decr_r_mr);
	    recv_msg(c);
	    send_mr(c->id, delete_r_mr);
	    recv_msg(c);
	}
	printf("Ascii reply cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec +
	            (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    }

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("Total cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
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
    int 	if_binary = 0;
    while (-1 != (c = getopt(argc, argv,
            "c:"    /* thread number */
            "r:"    /* request number per thread */
            "t:"    /* last time, secs */
            "p:"    /* listening port */
            "s:"    /* server ip */
	    "m:"    /* request size */
            "R"     /* whether receive message from server */
            "v"     /* verbose */
	    "b"     /* binary protocol */
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
	    case 'm':
	        request_size = atoi(optarg);
		break;
	    case 'b':
	        if_binary = 1;
            default:
                assert(0);
        }
    }

    init_rdma_global_resources();

    struct timespec start,
                    finish;
    clock_gettime(CLOCK_REALTIME, &start);

    init_message(if_binary);

    test_with_regmem(if_binary);

    clock_gettime(CLOCK_REALTIME, &finish);

    printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    return 0;
}

