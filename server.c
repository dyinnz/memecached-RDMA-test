/***************************************************************************//**
 * @file server.h
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <event.h>

// temp
struct rdma_cm_id *last_id;

/***************************************************************************//**
 * Settings
 *
 ******************************************************************************/

#define MAX_BUFF_SIZE 1024
#define REG_PER_CONN 10
#define REG_NUM 10
#define REG_SIZE 128

struct rdma_context {
    struct ibv_context          **device_ctx_list;
    struct ibv_context          *device_ctx;
    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_cq               *cq;

    struct rdma_event_channel   *cm_channel;
    
    struct rdma_cm_id           *listen_id;

    struct event_base           *base;
    struct event                listen_event;
} rdma_ctx;

struct rdma_conn {
    struct rdma_cm_id       *id;

    struct ibv_mr           *smr;
    char                    *sbuf;
    size_t                  ssize;

    struct ibv_mr           **rmr_list;
    char                    **rbuf_list;
    size_t                  rsize; 
    size_t                  buff_list_size;

    struct event            poll_event;
};

static int      backlog = 1024;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 8;
static char     *port = "5555";
static int      request_num = 1000;
static int      verbose = 0;

static char recv_buffs[REG_NUM][REG_SIZE];
static struct ibv_mr *recv_mrs[REG_NUM];
static struct ibv_recv_wr recv_wrs[REG_NUM];
static struct ibv_sge recv_sge[REG_NUM];

void rdma_cm_event_handle(int fd, short lib_event, void *arg);
void poll_event_handle(int fd, short lib_event, void *arg);

/***************************************************************************//**
 * Description 
 * Init rdma global resources
 *
 ******************************************************************************/
int
init_rdma_global_resources() {
    int num_device;
    if ( !(rdma_ctx.device_ctx_list = rdma_get_devices(&num_device)) ) {
        perror("rdma_get_devices()");
        return -1;
    }
    rdma_ctx.device_ctx = *rdma_ctx.device_ctx_list;
    printf("Get device: %d\n", num_device); 

    if ( !(rdma_ctx.comp_channel = ibv_create_comp_channel(rdma_ctx.device_ctx)) ) {
        perror("ibv_create_comp_channel");
        return -1;
    }

    if ( !(rdma_ctx.pd = ibv_alloc_pd(rdma_ctx.device_ctx)) ) {
        perror("ibv_alloc_pd");
        return -1;
    }

    if ( !(rdma_ctx.cq = ibv_create_cq(rdma_ctx.device_ctx, 
                    cq_size, NULL, rdma_ctx.comp_channel, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    if (0 != ibv_req_notify_cq(rdma_ctx.cq, 0)) {
        perror("ibv_reg_notify_cq");
        return -1;
    }

    int i = 0;
    for (i = 0; i < REG_NUM; ++i) {
        if (!(recv_mrs[i] = ibv_reg_mr(rdma_ctx.pd, recv_buffs[i], REG_SIZE, IBV_ACCESS_LOCAL_WRITE))) {
            perror("ibv_reg_mr()");
            return -1;
        }
        recv_sge[i].addr = (uint64_t)recv_mrs[i]->addr;
        recv_sge[i].length = recv_mrs[i]->length;
        recv_sge[i].lkey = recv_mrs[i]->lkey;

        recv_wrs[i].num_sge = 1;
        recv_wrs[i].sg_list = &(recv_sge[i]);
        recv_wrs[i].wr_id = (uint64_t)recv_mrs[i];
        recv_wrs[i].next = NULL;

        printf("mr: %p,  addr: %p\n", (void*)recv_mrs[i], recv_mrs[i]->addr);
    }

    return 0;
}

/***************************************************************************//**
 * Return
 * 0 on success, -1 on failure
 *
 * Description
 * Init resources required for listening, and build listeninng.
 *
 ******************************************************************************/
int
init_rdma_listen() {
    if (0 != rdma_create_id(NULL, &rdma_ctx.listen_id, NULL, RDMA_PS_TCP)) {
        perror("rdma_create_id()");
        return -1;
    }

    struct rdma_addrinfo    hints,
                            *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = RAI_PASSIVE;
    hints.ai_port_space = RDMA_PS_TCP;
    if (0 != rdma_getaddrinfo(NULL, port, &hints, &res)) {
        perror("rdma_addrinfo");
        return -1;
    }
    int ret = rdma_bind_addr(rdma_ctx.listen_id, res->ai_src_addr);
    rdma_freeaddrinfo(res);
    if (0 != ret) {
        perror("rdma_bind_addr()");
        return -1;
    }

    if ( !(rdma_ctx.cm_channel = rdma_create_event_channel() ) ) {
        perror("rdma_create_event_channel");
        return 0;
    }

    if (0 != rdma_migrate_id(rdma_ctx.listen_id, rdma_ctx.cm_channel)) {
        perror("rdma_migrate_id");
        return -1;
    }

    if (0 != rdma_listen(rdma_ctx.listen_id, backlog)) {
        perror("rdma_listen");
        return -1;
    }

    printf("Listening on port %d\n", ntohs(rdma_get_src_port(rdma_ctx.listen_id)) );

    return 0;
}

/***************************************************************************//**
 * Description
 * Create shared resources for RDMA operations
 *
 ******************************************************************************/
int
init_and_dispatch_event() {
    rdma_ctx.base = event_base_new();
    memset(&rdma_ctx.listen_event, 0, sizeof(struct event));

    /* TODO */
    event_set(&rdma_ctx.listen_event, rdma_ctx.cm_channel->fd, EV_READ | EV_PERSIST, 
            rdma_cm_event_handle, NULL);
    event_base_set(rdma_ctx.base, &rdma_ctx.listen_event);
    event_add(&rdma_ctx.listen_event, NULL);

    /* main loop */
    event_base_dispatch(rdma_ctx.base);

    return 0;
}


/***************************************************************************//**
 * Description
 * Release cc, pd, cq
 *
 ******************************************************************************/
void
release_conn(struct rdma_conn *c) {
    if (c->smr) rdma_dereg_mr(c->smr);
    if (c->sbuf) free(c->sbuf);

    int i = 0;
    for (i = 0; i < c->buff_list_size; ++i) {
        rdma_dereg_mr(c->rmr_list[i]);
        free(c->rbuf_list[i]);
    }
    if (c->rmr_list) free(c->rmr_list);
    if (c->rbuf_list) free(c->rbuf_list);

    free(c);
}


/***************************************************************************//**
 * Description
 * Create qp with id, then complete the connection 
 *
 ******************************************************************************/
int 
handle_connect_request(struct rdma_cm_id *id) {
    struct rdma_conn *c = calloc(1, sizeof(struct rdma_conn));
    id->context = c;
    c->id = id;

    struct ibv_qp_init_attr init_qp_attr;
    memset(&init_qp_attr, 0, sizeof(init_qp_attr)); 
    init_qp_attr.cap.max_send_wr = 8;
    init_qp_attr.cap.max_recv_wr = wr_size;
    init_qp_attr.cap.max_send_sge = max_sge;
    init_qp_attr.cap.max_recv_sge = max_sge;
    init_qp_attr.sq_sig_all = 1;
    init_qp_attr.qp_type = IBV_QPT_RC;
    init_qp_attr.send_cq = rdma_ctx.cq;
    init_qp_attr.recv_cq = rdma_ctx.cq;

    if (0 != rdma_create_qp(id, rdma_ctx.pd, &init_qp_attr)) {
        perror("rdma_create_qp");
        return -1;
    }

    if (0 != rdma_accept(id, NULL)) {
        perror("rdma_accept");
        return -1;
    }
    /* temp */
    last_id = id;

    event_set(&c->poll_event, rdma_ctx.comp_channel->fd, EV_READ | EV_PERSIST, 
            poll_event_handle, c);
    event_base_set(rdma_ctx.base, &c->poll_event);
    event_add(&c->poll_event, NULL);

    int i = 0;
    /*
    struct ibv_recv_wr *bad;
    for (i = 0; i < REG_NUM; ++i) {
        if (0 != ibv_post_recv(id->qp, &recv_wrs[i], &bad)) {
            perror("rdma_post_recv");
            return -1;
        }
    }
    */

    c->buff_list_size = REG_PER_CONN;
    c->rsize = MAX_BUFF_SIZE;
    c->rbuf_list = calloc(c->buff_list_size, sizeof(char*));
    c->rmr_list = calloc(c->buff_list_size, sizeof(struct ibv_mr*));

    for (i = 0; i < c->buff_list_size; ++i) {
        c->rbuf_list[i] = malloc(c->rsize);
        c->rmr_list[i] = rdma_reg_msgs(id, c->rbuf_list[i], c->rsize);
        if (0 != rdma_post_recv(id, c->rmr_list[i], c->rmr_list[i]->addr, c->rmr_list[i]->length, c->rmr_list[i])) {
            perror("rdma_post_recv()");
            return -1;
        }
    }

    return 0;
}

/***************************************************************************//**
 * Description
 * Candle "work complete"
 *
 ******************************************************************************/
void 
handle_work_complete(struct ibv_wc *wc) {
    struct ibv_mr *mr = (struct ibv_mr*)wc->wr_id;

    if (IBV_WC_SUCCESS != wc->status) {
        printf("bad wc!\n");
        return;
    }

    if (IBV_WC_RECV & wc->opcode) {
        static int count = 0;
        //if (++count % 200 == 0) {
            printf("server has received: mr: %p, addr: %p\n%s\n", (void*)mr, (void*)mr->addr, (char*)mr->addr);
        //}
        memset(mr->addr, 0, mr->length);
        if (0 != rdma_post_recv(last_id, mr, mr->addr, mr->length, mr)) {
            perror("rdma_post_recv()");
            return;
        }
        return;
    }

    switch (wc->opcode) {
        case IBV_WC_SEND:
            printf("server has sent: %s\n", (char*)mr->addr);
            break;
        case IBV_WC_RDMA_WRITE:
            break;
        case IBV_WC_RDMA_READ:
            break;
        default:
            break;
    }
}


/***************************************************************************//**
 * Description
 *
 ******************************************************************************/
void 
poll_event_handle(int fd, short lib_event, void *arg) {
    //struct rdma_conn        *c = arg;
    struct ibv_cq           *cq = NULL;
    struct ibv_wc           wc[10];

    int     cqe = 0, i = 0;
    void    *null = NULL;

    memset(&cq, 0, sizeof(cq));
    memset(wc, 0, sizeof(wc));

    if (0 != ibv_get_cq_event(rdma_ctx.comp_channel, &cq, &null)) {
        perror("ibv_get_cq_event");
        return;
    }
    ibv_ack_cq_events(cq, 1);

    if (0 != ibv_req_notify_cq(cq, 0)) {
        perror("ibv_reg_notify_cq");
        return;
    }

    if ( -1 == (cqe = ibv_poll_cq(cq, 10, wc)) ) {
        perror("ibv_poll_cq");
        return;
    }

    for (i = 0; i < cqe; ++i) {
        handle_work_complete(wc);
    }
}


/***************************************************************************//**
 * Description
 *
 ******************************************************************************/
void 
rdma_cm_event_handle(int fd, short lib_event, void *arg) {
    struct rdma_cm_event    *cm_event = NULL;
    struct rdma_conn        *c = NULL;

    if (0 != rdma_get_cm_event(rdma_ctx.cm_channel, &cm_event)) {
        perror("rdma_get_cm_event");
        return;
    }
    printf("%s\n", rdma_event_str(cm_event->event));

    c = cm_event->id->context;

    switch (cm_event->event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            if (0 != handle_connect_request(cm_event->id)) {
                rdma_disconnect(cm_event->id);
            }
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
           break;

        case RDMA_CM_EVENT_DISCONNECTED:
            release_conn(c);
            break;

        case RDMA_CM_EVENT_ADDR_ERROR:
        case RDMA_CM_EVENT_ROUTE_ERROR:
        case RDMA_CM_EVENT_CONNECT_ERROR:
        case RDMA_CM_EVENT_UNREACHABLE:
        case RDMA_CM_EVENT_REJECTED:
            printf("CM event error: %d\n", cm_event->status);
            break;
	
        default:
            printf("Ingoring this event\n");
            break;
    }

    rdma_ack_cm_event(cm_event);
}

/***************************************************************************//**
 * Main
 *
 ******************************************************************************/
int 
main(int argc, char *argv[]) {

    memset(&rdma_ctx, 0, sizeof(struct rdma_context));

    if (0 != init_rdma_global_resources()) return -1;
    if (0 != init_rdma_listen()) return -1;
    if (0 != init_and_dispatch_event()) return -1;

    return 0;
}

