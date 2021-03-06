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

#define REG_PER_CONN 128
#define POLL_WC_SIZE 128
#define BUFF_SIZE 1024

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

struct wr_context {
    struct rdma_conn       *c;
    struct ibv_mr           *mr;
};

struct rdma_conn {
    struct rdma_cm_id       *id;

    struct ibv_mr           *smr;
    char                    *sbuf;
    size_t                  ssize;
    struct wr_context       swr;

    struct ibv_mr           **rmr_list;
    char                    **rbuf_list;
    struct wr_context       *wr_ctx_list;
    size_t                  rsize; 
    size_t                  buff_list_size;

    struct event            poll_event;

    int                     total_recv;
};


static int      backlog = 1024;
static int      cq_size = 1024;
static int      wr_size = 1024;
static int      max_sge = 8;
static char     *port = "6666";
static int      request_num = 1000;
static int      verbose = 0;

int init_rdma_global_resources();
int init_rdma_listen();
int init_and_dispatch_event();
void release_conn(struct rdma_conn *c);
int handle_connect_request(struct rdma_cm_id *id);
void handle_work_complete(struct ibv_wc *wc);
void post_larger_memory(struct rdma_conn *c, struct ibv_mr *mr);
void handle_rdma_read_request(struct rdma_conn *c, const char *head);

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

    c->ssize = 128;
    c->sbuf = malloc(c->ssize);
    sprintf(c->sbuf, "hello client!\n");
    if ( !(c->smr = rdma_reg_msgs(id, c->sbuf, c->ssize)) ) {
        perror("rdma_reg_msgs()");
        return -1;
    }

    c->buff_list_size = REG_PER_CONN;
    c->rsize = BUFF_SIZE;
    c->rbuf_list = calloc(c->buff_list_size, sizeof(char*));
    c->rmr_list = calloc(c->buff_list_size, sizeof(struct ibv_mr*));
    c->wr_ctx_list = calloc(c->buff_list_size, sizeof(struct wr_context));

    /*
    size_t large_size = 10240;
    char *buff = malloc(large_size);
    struct ibv_mr *new_mr = rdma_reg_msgs(c->id, buff, large_size);
    struct wr_context *wr_ctx = malloc(sizeof(struct wr_context));
    wr_ctx->c = c;
    wr_ctx->mr = new_mr;
    if (0 != rdma_post_recv(c->id, wr_ctx, new_mr->addr, new_mr->length, new_mr)) {
        perror("rdma_post_recv() in post_larger_memory()");
        return;
    }
    */


    int i = 0;
    for (i = 0; i < c->buff_list_size; ++i) {
        c->rbuf_list[i] = malloc(c->rsize);
        c->rmr_list[i] = rdma_reg_msgs(id, c->rbuf_list[i], c->rsize);
        c->wr_ctx_list[i].c = c;
        c->wr_ctx_list[i].mr = c->rmr_list[i];
        if (0 != rdma_post_recv(id, &c->wr_ctx_list[i], c->rmr_list[i]->addr, c->rmr_list[i]->length, c->rmr_list[i])) {
            perror("rdma_post_recv()");
            return -1;
        }
    }

    event_set(&c->poll_event, rdma_ctx.comp_channel->fd, EV_READ | EV_PERSIST, 
            poll_event_handle, NULL);
    event_base_set(rdma_ctx.base, &c->poll_event);
    event_add(&c->poll_event, NULL);

    return 0;
}

/***************************************************************************//**
 * Description
 * Candle "work complete"
 *
 ******************************************************************************/
void 
handle_work_complete(struct ibv_wc *wc) {
    //struct ibv_mr *mr = ((struct ibv_mr*)(uintptr_t)wc->wr_id);
    struct wr_context *wr_ctx = (struct wr_context *)(uintptr_t)wc->wr_id;
    struct ibv_mr *mr = wr_ctx->mr;
    struct rdma_conn *c = wr_ctx->c;

    if (IBV_WC_SUCCESS != wc->status) {
        printf("BAD WC [%d], buff length: %zd\n", (int)wc->status, mr->length);
        if (IBV_WC_LOC_LEN_ERR == wc->status) {
            post_larger_memory(c, mr);
        }
        return;
    }

    if (IBV_WC_RECV & wc->opcode) {
        c->total_recv += 1;
        if (c->total_recv % 1000 == 0) {
            printf("server has received %d : %s\n", c->total_recv, (char*)mr->addr);
        }
        if (verbose) {
            printf("server has received %d : %s\n", c->total_recv, (char*)mr->addr);
        }
        if (0 != rdma_post_recv(c->id, (void *)(uintptr_t)wc->wr_id, mr->addr, mr->length, mr)) {
            perror("rdma_post_recv()");
            return;
        }
        
        if ('\x88' == ((char*)mr->addr)[0]) {
            handle_rdma_read_request(c, mr->addr);
        }

        if (NULL == strstr(mr->addr, "noreply")) {
            c->swr.c = c;
            c->swr.mr = c->smr;
            if (0 != rdma_post_send(c->id, &c->swr, c->smr->addr, c->smr->length, c->smr, 0)) {
                perror("rdma_post_send()");
                return;
            }
            //printf("post send ok\n");
        }
        return;
    }

    switch (wc->opcode) {
        case IBV_WC_SEND:
            //printf("server has sent: %s\n", (char*)mr->addr);
            break;
        case IBV_WC_RDMA_WRITE:
            break;
        case IBV_WC_RDMA_READ:
            c->swr.c = c;
            c->swr.mr = c->smr;
            if (0 != rdma_post_send(c->id, &c->swr, c->smr->addr, c->smr->length, c->smr, 0)) {
                perror("rdma_post_send()");
                return;
            }
            break;
        default:
            break;
    }
}

/***************************************************************************//**
 *  
 ******************************************************************************/
void
post_larger_memory(struct rdma_conn *c, struct ibv_mr *mr) {
    size_t large_size = mr->length * 20;
    char *buff = malloc(large_size);
    struct ibv_mr *new_mr = rdma_reg_msgs(c->id, buff, large_size);
    struct wr_context *wr_ctx = malloc(sizeof(struct wr_context));
    wr_ctx->c = c;
    wr_ctx->mr = new_mr;
    if (0 != rdma_post_recv(c->id, wr_ctx, new_mr->addr, new_mr->length, new_mr)) {
        perror("rdma_post_recv() in post_larger_memory()");
        return;
    }
}

/***************************************************************************//**
 *  
 ******************************************************************************/
void 
handle_rdma_read_request(struct rdma_conn *c, const char *head) {
    uint64_t addr = 0;
    uint32_t rkey = 0;
    uint32_t length = 0;
    printf("HEAD: %s\n", head);
    sscanf(head+2, "%llu %u %u\n", &addr, &rkey, &length);

    char *buff = malloc(length);
    struct ibv_mr *large_mr = rdma_reg_msgs(c->id, buff, length);
    struct wr_context *wr_ctx = malloc(sizeof(struct wr_context));
    wr_ctx->c = c;
    wr_ctx->mr = large_mr;
    if (0 != rdma_post_read(c->id, wr_ctx, large_mr->addr, large_mr->length, large_mr, IBV_SEND_SIGNALED, addr, rkey)) {
        perror("rdma_post_read()");
        return;
    }

    printf("Post read ok\n");
}

/***************************************************************************//**
 * Description
 *
 ******************************************************************************/
void 
poll_event_handle(int fd, short lib_event, void *arg) {
    //struct rdma_conn        *c = arg;
    struct ibv_cq           *cq = NULL;
    struct ibv_wc           wc[POLL_WC_SIZE];

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

    do {
        if ( -1 == (cqe = ibv_poll_cq(cq, POLL_WC_SIZE, wc)) ) {
            perror("ibv_poll_cq");
            return;
        }

        for (i = 0; i < cqe; ++i) {
            handle_work_complete(&wc[i]);
        }
    } while (cqe == POLL_WC_SIZE);
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
            printf("total recv: %d\n", c->total_recv);
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

