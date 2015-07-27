#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libxio.h"
#include <event2/event.h>
#include <event2/event_struct.h>

char send_buff[] = "hello word head response";

/*----------------------------------------------------------------------------*
 * struct server_data 
 *----------------------------------------------------------------------------*/
struct server_data {
    struct xio_context      *context;
    struct xio_connection   *connection;
    struct xio_msg          *send_msg;
};

/*----------------------------------------------------------------------------*
 * generic error event notification
 *----------------------------------------------------------------------------*/
static int
on_session_event(struct xio_session             *session, 
                 struct xio_session_event_data  *event_data,
                 void                           *cb_usr_context) {
    struct server_data *server_data = cb_usr_context;
    printf("session event: %s; reason: %s\n", xio_session_event_str(event_data->event),
            xio_strerror(event_data->reason));

    switch (event_data->event) {
    case XIO_SESSION_NEW_CONNECTION_EVENT:
        server_data->connection = event_data->conn;
        break;
    case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
        xio_connection_destroy(event_data->conn);
        break;
    case XIO_SESSION_TEARDOWN_EVENT:
        xio_session_destroy(session);
        break;
    default:
        break;
    }

    return 0;
}

/*----------------------------------------------------------------------------*
 * on_new_session 
 *----------------------------------------------------------------------------*/
static int 
on_new_session(struct xio_session *session, struct xio_new_session_req *req,
			  void *cb_user_context) {

    printf("new session event.");
    xio_accept(session, NULL, 0, NULL, 0);

    return 0;
}

/*----------------------------------------------------------------------------*
 * on_session_established 
 * client only
 *----------------------------------------------------------------------------*/
static int 
on_session_established(struct xio_session *session,
				      struct xio_new_session_rsp *rsp,
				      void *cb_user_context) {
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg_send_comlete 
 *----------------------------------------------------------------------------*/
static int 
on_msg_send_complete(struct xio_session *session, struct xio_msg *rsp,
				    void *conn_user_context) {
    printf("on_msg_send_complete");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg 
 *----------------------------------------------------------------------------*/
static int 
on_msg(struct xio_session *session, struct xio_msg *msg, int last_in_rxq,
        void *conn_user_context) {

    struct server_data      *server_data = conn_user_context;
    struct xio_iovec_ex     *sglist = vmsg_sglist(&msg->in);
    char                    *str = NULL;
    int                     nents = vmsg_sglist_nents(&msg->in);
    int                     len = 0, i = 0;

    printf("on_msg");

    str = msg->in.header.iov_base;
    len = msg->in.header.iov_len;
    if (str) {
        printf("message header: %s\n", str);
    }

    for (i = 0; i < nents; ++i) {
        str = sglist[i].iov_base;
        len = sglist[i].iov_len;
        if (str) {
            printf("message header: %s\n", str);
        }
    }

    vmsg_sglist_set_nents(&msg->in, 0);

    xio_send_response(server_data->send_msg);

    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg_delivered 
 *----------------------------------------------------------------------------*/
static int on_msg_delivered(struct xio_session *session, struct xio_msg *msg,
        int last_in_rxq, void *conn_user_context) {
    printf("on_msg_delivered");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg_error 
 *----------------------------------------------------------------------------*/
static int 
on_msg_error(struct xio_session *session, 
        enum xio_status error,
        enum xio_msg_direction direction,
        struct xio_msg  *msg,
        void *conn_user_context) {
    printf("on_msg_error");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_cancel 
 *----------------------------------------------------------------------------*/
static int 
on_cancel(struct xio_session *session,
			 struct xio_msg  *msg,
			 enum xio_status result,
			 void *conn_user_context) {
    printf("on_cancel");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_cancel_request 
 *----------------------------------------------------------------------------*/
static int 
on_cancel_request(struct xio_session *session,
				 struct xio_msg  *msg,
				 void *conn_user_context) {
    printf("on_cancel_request");
    return 0;
}

/*----------------------------------------------------------------------------*
 * assign_data_in_buff 
 *----------------------------------------------------------------------------*/
static int 
assign_data_in_buf(struct xio_msg *msg,
				  void *conn_user_context) {
    printf("assign_data_in_buf");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_ow_msg_send_complete 
 *----------------------------------------------------------------------------*/
static int 
on_ow_msg_send_complete(struct xio_session *session,
				       struct xio_msg *msg,
				       void *conn_user_context) {
    printf("on_ow_msg_send_complete");
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_rdma_direct_complete 
 *----------------------------------------------------------------------------*/
static int 
on_rdma_direct_complete(struct xio_session *session,
				       struct xio_msg *msg,
				       void *conn_user_context) {
    printf("on_rdma_direct_complete");
    return 0;
}

/*-----------------------------------------------------------------------------
 * Asynchronous callbacks
 *----------------------------------------------------------------------------*/

static struct xio_session_ops  server_ops = {
	.on_session_event		= on_session_event,
	.on_new_session			= on_new_session,
    .on_session_established = on_session_established,
	.on_msg_send_complete   = on_msg_send_complete,
	.on_msg				    = on_msg,
    .on_msg_delivered       = on_msg_delivered,
	.on_msg_error			= on_msg_error,
    .on_cancel              = on_cancel,
    .on_cancel_request      = on_cancel_request,
    .assign_data_in_buf     = assign_data_in_buf,
    .on_ow_msg_send_complete = on_ow_msg_send_complete,
    .on_rdma_direct_complete = on_rdma_direct_complete,
};

/*----------------------------------------------------------------------------*
 * xio_event_handler 
 *----------------------------------------------------------------------------*/
static void 
xio_event_handler(int fd, short event_flag, void *arg) {

    struct xio_context *context = arg;

    xio_context_poll_wait(context, 0);
}

/*----------------------------------------------------------------------------*
 *  main
 *----------------------------------------------------------------------------*/

int main() {
    struct xio_context      *context = NULL;
    struct xio_server       *server = NULL;

    struct event_base       *base = NULL;
    struct event            main_event;

    struct server_data      *server_data;

    int         xio_fd = 0;

    xio_init();

    if ( !(context = xio_context_create(NULL, 0, -1)) ) {
        perror("xio_context_create");
        return -1;
    }

    if ( -1 == (xio_fd = xio_context_get_poll_fd(context)) ) {
        perror("xio_context_get_poll_fd");
        return -1;
    }

    if ( !(server = xio_bind(context, &server_ops, "rdma://127.0.0.1:5555", NULL, 0, &server_data)) ) {
        perror("xio_bind");
        return -1;
    }

    server_data->send_msg = calloc(0, sizeof(struct xio_msg));
    server_data->send_msg->out.header.iov_base = send_buff;
    server_data->send_msg->out.header.iov_len = sizeof(send_buff);

    base = event_base_new();
    memset(&main_event, 0, sizeof(struct event));
    event_assign(&main_event, base, xio_fd, EV_READ | EV_PERSIST, xio_event_handler, &context);

    event_add(&main_event, NULL);
    event_base_dispatch(base);

    // Release
    event_base_free(base);
    xio_unbind(server);
    xio_context_destroy(context);
    xio_shutdown();

    return 0;
}
