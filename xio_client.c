#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <libxio.h>

/*----------------------------------------------------------------------------*
 * struct server_data 
 *----------------------------------------------------------------------------*/
struct client_context {
    struct xio_context      *context;
    struct xio_connection   *connection;
    struct xio_msg          *send_msg;
} client_ctx;

/*----------------------------------------------------------------------------*
 * generic error event notification
 *----------------------------------------------------------------------------*/
static int
on_session_event(struct xio_session             *session, 
                 struct xio_session_event_data  *event_data,
                 void                           *cb_usr_context) {
    printf("%s\n", __func__);

    printf("session event: %s. reason: %s\n",
            xio_session_event_str(event_data->event),
            xio_strerror(event_data->reason));

    switch (event_data->event) {
        case XIO_SESSION_CONNECTION_TEARDOWN_EVENT:
            xio_connection_destroy(event_data->conn);
            break;
        case XIO_SESSION_TEARDOWN_EVENT:
            xio_session_destroy(session);
            break;
        default:
            break;
    };

    return 0;
}

/*----------------------------------------------------------------------------*
 * on_new_session 
 *----------------------------------------------------------------------------*/
static int 
on_new_session(struct xio_session *session, struct xio_new_session_req *req,
              void *cb_user_context) {
    printf("%s\n", __func__);
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
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg_send_comlete 
 *----------------------------------------------------------------------------*/
static int 
on_msg_send_complete(struct xio_session *session, struct xio_msg *rsp,
                    void *conn_user_context) {
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg 
 *----------------------------------------------------------------------------*/
static int 
on_msg(struct xio_session *session, struct xio_msg *msg, int last_in_rxq,
        void *conn_user_context) {
    printf("%s\n", __func__);

    struct xio_iovec_ex     *sglist = vmsg_sglist(&msg->in);
    int                     nents = vmsg_sglist_nents(&msg->in);
    int                     i = 0;

    str = msg->in.header.iov_base;
    if (str) {
        printf("HEADER: %s;\nDATA NUM:%d\n", str, nents);
    }

    for (i = 0; i < nents; ++i) {
        str = sglist[i].iov_base;
        if (str) {
            printf("DATA: %s\n", str);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_msg_delivered 
 *----------------------------------------------------------------------------*/
static int on_msg_delivered(struct xio_session *session, struct xio_msg *msg,
        int last_in_rxq, void *conn_user_context) {
    printf("%s\n", __func__);
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
    printf("%s\n", __func__);
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
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_cancel_request 
 *----------------------------------------------------------------------------*/
static int 
on_cancel_request(struct xio_session *session,
                 struct xio_msg  *msg,
                 void *conn_user_context) {
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * assign_data_in_buff 
 *----------------------------------------------------------------------------*/
static int 
assign_data_in_buf(struct xio_msg *msg,
                  void *conn_user_context) {
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_ow_msg_send_complete 
 *----------------------------------------------------------------------------*/
static int 
on_ow_msg_send_complete(struct xio_session *session,
                       struct xio_msg *msg,
                       void *conn_user_context) {
    printf("%s\n", __func__);
    return 0;
}

/*----------------------------------------------------------------------------*
 * on_rdma_direct_complete 
 *----------------------------------------------------------------------------*/
static int 
on_rdma_direct_complete(struct xio_session *session,
                       struct xio_msg *msg,
                       void *conn_user_context) {
    printf("%s\n", __func__);
    return 0;
}

/*-----------------------------------------------------------------------------
 * Asynchronous callbacks
 *----------------------------------------------------------------------------*/

static struct xio_session_ops  client_ops = {
    .on_session_event        = on_session_event,
    .on_new_session            = on_new_session,
    .on_session_established = on_session_established,
    .on_msg_send_complete   = on_msg_send_complete,
    .on_msg                    = on_msg,
    .on_msg_delivered       = on_msg_delivered,
    .on_msg_error            = on_msg_error,
    .on_cancel              = on_cancel,
    .on_cancel_request      = on_cancel_request,
    .assign_data_in_buf     = assign_data_in_buf,
    .on_ow_msg_send_complete = on_ow_msg_send_complete,
    .on_rdma_direct_complete = on_rdma_direct_complete,
};

/*----------------------------------------------------------------------------*
 *  main
 *----------------------------------------------------------------------------*/

static char uri[1024] = "";
static int request_number = 1000;
static char *port = "6666";
static char *server = "0.0.0.0";

int main(int argc, char *argv[]) {
    char        c = '\0';
    while (-1 != (c = getopt(argc, argv,
            "r:"    /* request number per thread */
            "p:"    /* listening port */
            "s:"    /* server ip */
    ))) {
        switch (c) {
            case 'r':
                request_number = atoi(optarg);
                break;
            case 'p':
                port = optarg;
                break;
            case 's':
                server = optarg;
                break;
            default:
                assert(0);
        }
    }
    sprintf(uri, "rdma://%s:%s", server, port);

    xio_init();
    client_ctx.context = xio_context_create(NULL, 0, -1);

    struct xio_session_params session_params;
    memset(&session_params, 0, sizeof(struct xio_session_params));
    session_params.type = XIO_SESSION_CLIENT;
    session_params.ses_ops = &client_ops;
    session_params.user_context = &client_ctx;
    session_params.uri = uri;

    struct xio_session *session = xio_session_create(&session_params);

    struct xio_connection_params conn_params;
    memset(&conn_params, 0, sizeof(struct xio_connection_params));
    conn_params.session = session;
    conn_params.ctx = client_ctx.context;
    conn_params.conn_user_context = &client_ctx;

    struct xio_connection *conn = xio_connect(&conn_params);

    struct xio_msg test_send;
    memset(&test_send, 0, sizeof(test_send));
    test_send.out.header.iov_base = "hello world";
    test_send.out.header.iov_len = sizeof("hello world");

    test_send.in.sgl_type = XIO_SGL_TYPE_IOV;
    test_send.in.data_iov.max_nents = XIO_IOVLEN;

    test_send.out.sgl_type = XIO_SGL_TYPE_IOV;
    test_send.out.data_iov.max_nents = XIO_IOVLEN;

    test_send.out.data_iov.sglist[0].iov_base = strdup("this is client");
    test_send.out.data_iov.sglist[0].iov_len =
        strlen((const char *) test_send.out.data_iov.sglist[0].iov_base) + 1;
    test_send.out.data_iov.nents = 1;

    for (int i = 0; i < request_number; ++i) {
        xio_send_request(conn, &test_send);
    }

    xio_context_run_loop(client_ctx.context, XIO_INFINITE);

    return 0;
}
