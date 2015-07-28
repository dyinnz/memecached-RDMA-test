#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <event.h>

#define MAXLEN 16

struct Setting {
    int         cq_number;
    char        listen_port[10];    /* 10 is enough to hold the port */
};

struct RDMAContext {
    struct ibv_context          *device_context;
    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_cq               *cq;
    
    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *listen_id;

    struct event_base           *base;
    struct event                listen_event;
};

struct CMInformation {
    struct rdma_cm_id           *id;

    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_cq               *cq;
    struct ibv_mr               *send_mr;
    struct ibv_mr               *recv_mr;
    
    struct event                *poll_event;
};

void rdma_cm_event_handle(int fd, short lib_event, void *arg);
void poll_event_handle(int fd, short lib_event, void *arg);

/******************************************************************************
 * Test
 *
 *****************************************************************************/

char   recv_msg[MAXLEN] = "recive test!";
char   send_msg[MAXLEN] = "send test!";

struct RDMAContext *g_rdma_context = NULL;
struct Setting *g_setting = NULL;

/******************************************************************************
 * Description
 * Init struct Setting with default
 *
 *****************************************************************************/
void 
init_setting_with_default(struct Setting *setting) {
    setting->cq_number = 1024;
    strcpy(setting->listen_port, "5555");
}

/******************************************************************************
 *
 * Description
 * Release all resources
 *
 ******************************************************************************/
 
void release_resources(struct Setting *setting, struct RDMAContext *context) {
    event_base_free(context->base);

    rdma_destroy_id(context->listen_id); 
    rdma_destroy_event_channel(context->cm_channel);
/*
    ibv_destroy_cq(context->cq);
    ibv_dealloc_pd(context->pd);
    ibv_destroy_comp_channel(context->comp_channel);
*/
    free(setting);
    free(context);
}

/******************************************************************************
 * Return
 * 0 on success, -1 on failure
 *
 * Description
 * Init resources required for listening, and build listeninng.
 *
 *****************************************************************************/
int
init_rdma_listen(struct Setting *setting, struct RDMAContext *context) {
    struct rdma_addrinfo    hints,
                            *res = NULL;
	struct ibv_qp_init_attr attr;
    int                     ret = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
    if (0 != rdma_getaddrinfo(NULL, setting->listen_port, &hints, &res)) {
        perror("rdma_addrinfo");
        return -1;
    }

	memset(&attr, 0, sizeof attr);
	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
	attr.cap.max_inline_data = 16;
	attr.sq_sig_all = 1;
	
    ret = rdma_create_ep(&context->listen_id, res, NULL, &attr);
	rdma_freeaddrinfo(res);
	if (0 != ret) {
        perror("rdma_create_ep");
        return -1;
    }

    if ( !(context->cm_channel = rdma_create_event_channel() ) ) {
        perror("rdma_create_event_channel");
        return 0;
    }

    if (0 != rdma_migrate_id(context->listen_id, context->cm_channel)) {
        perror("rdma_migrate_id");
        return -1;
    }

    if (0 != rdma_listen(context->listen_id, 0)) {
        perror("rdma_listen");
        return -1;
    }

    printf("Listening on port %d\n", ntohs(rdma_get_src_port(context->listen_id)) );

    return 0;
}

/******************************************************************************
 * Return
 * 0 on success, -1 on failure
 *
 * Description
 * Create shared resources for RDMA operations
 *
 *****************************************************************************/
int
init_rdma_shared_resources(struct Setting *setting, struct RDMAContext *context) {

    if ( !(context->comp_channel = ibv_create_comp_channel(context->device_context)) ) {
        perror("ibv_create_comp_channel");
        return -1;
    }

    if ( !(context->pd = ibv_alloc_pd(context->device_context)) ) {
        perror("ibv_alloc_pd");
        return -1;
    }

    if ( !(context->cq = ibv_create_cq(context->device_context, 
                    setting->cq_number, NULL, context->comp_channel, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * Description
 * Create shared resources for RDMA operations
 *
 *****************************************************************************/
int
init_and_dispatch_event(struct RDMAContext *context) {
    context->base = event_base_new();
    memset(&context->listen_event, 0, sizeof(struct event));

    /* TODO */
    event_set(&context->listen_event, context->cm_channel->fd, EV_READ | EV_PERSIST, 
            rdma_cm_event_handle, NULL);
    event_base_set(context->base, &context->listen_event);
    event_add(&context->listen_event, NULL);

    /* main loop */
    event_base_dispatch(context->base);

    return 0;
}


/******************************************************************************
 * Description
 * Create cc, pd, and cq, remember releasing them
 *
 *****************************************************************************/
int
preamble_qp(struct ibv_context *device_context, struct CMInformation *info) {

    if ( !(info->comp_channel = ibv_create_comp_channel(device_context)) ) {
        perror("ibv_create_comp_channel");
        return -1;
    }

    if ( !(info->pd = ibv_alloc_pd(device_context)) ) {
        perror("ibv_alloc_pd");
        return -1;
    }

    if ( !(info->cq = ibv_create_cq(device_context, 
                    g_setting->cq_number, NULL, info->comp_channel, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    if (0 != ibv_req_notify_cq(info->cq, 0)) {
        perror("ibv_reg_notify_cq");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * Description
 * Release cc, pd, cq
 *
 *****************************************************************************/
void
release_cm_info(struct CMInformation *info) {
    ibv_destroy_cq(info->cq);
    ibv_dealloc_pd(info->pd);
    ibv_destroy_comp_channel(info->comp_channel);

    free(info);
}

/******************************************************************************
 * Description
 * Create qp with id, then complete the connection 
 *
 *****************************************************************************/
void 
handle_connect_request(struct rdma_cm_id *id) {
    struct CMInformation    *info = NULL;
    struct ibv_qp_init_attr init_qp_attr;

    info = calloc(1, sizeof(struct CMInformation));
    if (0 != preamble_qp(id->verbs, info)) {
        release_cm_info(info);
        return;
    }

    id->context = info;
    info->id    = id;

    memset(&init_qp_attr, 0, sizeof(init_qp_attr)); 
	init_qp_attr.cap.max_send_wr = g_setting->cq_number;
	init_qp_attr.cap.max_recv_wr = g_setting->cq_number;
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.sq_sig_all = 1;
	init_qp_attr.qp_type = IBV_QPT_RC;
	init_qp_attr.send_cq = info->cq;
	init_qp_attr.recv_cq = info->cq;

    if (0 != rdma_create_qp(id, info->pd, &init_qp_attr)) {
        release_cm_info(info);
        perror("rdma_create_qp");
        return;
    }

    if ( !(info->recv_mr = rdma_reg_msgs(id, recv_msg, MAXLEN)) ) {
        release_cm_info(info);
        perror("rdma_reg_msgs");
        return;
    }

    if (0 != rdma_post_recv(id, info, recv_msg, MAXLEN, info->recv_mr)) {
        release_cm_info(info);
        perror("rdma_post_recv");
        return;
    }

    if (0 != rdma_accept(id, NULL)) {
        release_cm_info(info);
        perror("rdma_accept");
        return;
    }

    printf("Comlete connection!\n");
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void
establish_poll_handler(struct rdma_cm_id *id) {
    struct CMInformation *info = id->context;

    info->poll_event = calloc(1, sizeof(struct event)); 

    event_set(info->poll_event, info->comp_channel->fd, EV_READ | EV_PERSIST, 
            poll_event_handle, info);
    event_base_set(g_rdma_context->base, info->poll_event);
    event_add(info->poll_event, NULL);
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void 
handle_work_complete(struct ibv_wc *wc) {
    struct CMInformation *info = (struct CMInformation*)wc->wr_id;

    if (IBV_WC_SUCCESS != wc->status) {
        printf("bad wc!\n");
        return;
    }

    if (IBV_WC_RECV & wc->opcode) {
        printf("server has received: %s\n", (char*)info->recv_mr->addr);
        return;
    }

    switch (wc->opcode) {
        case IBV_WC_SEND:
            printf("server has sent: %s\n", (char*)info->send_mr->addr);
            break;
        case IBV_WC_RDMA_WRITE:
            break;
        case IBV_WC_RDMA_READ:
            break;
        default:
            break;
    }
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void 
poll_event_handle(int fd, short lib_event, void *arg) {
    struct CMInformation    *info = arg;
    struct ibv_cq           *cq = NULL;
    struct ibv_wc           wc[10];

    int     cqe = 0, i = 0;
    void    *null = NULL;

    memset(&cq, 0, sizeof(cq));
    memset(wc, 0, sizeof(wc));

    if (0 != ibv_get_cq_event(info->comp_channel, &cq, &null)) {
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

    printf("Get cqe! %d\n", cqe);

    for (i = 0; i < cqe; ++i) {
        handle_work_complete(wc);
    }
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void 
rdma_cm_event_handle(int fd, short lib_event, void *arg) {
    struct rdma_cm_event    *cm_event = NULL;
    struct CMInformation    *info = NULL;

    if (0 != rdma_get_cm_event(g_rdma_context->cm_channel, &cm_event)) {
        perror("rdma_get_cm_event");
        return;
    }

    printf("---> %s\n", rdma_event_str(cm_event->event));

    switch (cm_event->event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            handle_connect_request(cm_event->id);
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            info = (struct CMInformation*)(cm_event->id->context);

            if ( !(info->send_mr = rdma_reg_msgs(cm_event->id, send_msg, MAXLEN)) ) {
                perror("rdma_reg_msgs");
                return;
            }

            if (0 != rdma_post_send(cm_event->id, info, send_msg, MAXLEN, 
                        info->send_mr, IBV_SEND_SOLICITED)) {
                perror("rdma_post_recv");
                return;
            }

            establish_poll_handler(cm_event->id);
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            info = (struct CMInformation*)(cm_event->id->context);
            rdma_dereg_mr(info->send_mr);
            rdma_dereg_mr(info->recv_mr);
            release_cm_info(info);

            rdma_disconnect(cm_event->id);
            break;

        case RDMA_CM_EVENT_ADDR_ERROR:
        case RDMA_CM_EVENT_ROUTE_ERROR:
        case RDMA_CM_EVENT_CONNECT_ERROR:
        case RDMA_CM_EVENT_UNREACHABLE:
        case RDMA_CM_EVENT_REJECTED:
            printf("error: %d\n", cm_event->status);
	
        default:
            printf("---> ingoring\n");
            break;
    }

    rdma_ack_cm_event(cm_event);
}

int main(int argc, char *argv[]) {

    struct Setting      *setting = calloc(1, sizeof(struct Setting));
    struct RDMAContext  *context = calloc(1, sizeof(struct RDMAContext)); 

    g_rdma_context = context;
    g_setting = setting;

    init_setting_with_default(setting);

    if (0 != init_rdma_listen(setting, context)) return -1;
/*    if (0 != init_rdma_shared_resources(setting, context)) return -1; */
    if (0 != init_and_dispatch_event(context)) return -1;
    

    release_resources(setting, context);
    return 0;
}


