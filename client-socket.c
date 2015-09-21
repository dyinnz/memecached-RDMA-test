#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>

#include "protocol_binary.h"
#include "build_cmd.h"

#define BUFF_SIZE 1024

/***************************************************************************//**
 * Testing parameters
 *
 ******************************************************************************/
static char     *pstr_server = "127.0.0.1";
static char     *pstr_port = "11211";
static int      thread_number = 1;
static int      request_number = 100000;
static int 	if_binary = 0;
static int      last_time = 1000;    /* secs */
static int      verbose = 0;
static int 	sock;



int socket_build_connection(void)
{
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, pstr_server, &server_addr.sin_addr);
    server_addr.sin_port = htons(atoi(pstr_port));

    return connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
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
    void *recv_buff = malloc(BUFF_SIZE);

    clock_gettime(CLOCK_REALTIME, &start);
    if (socket_build_connection()) {
        return NULL;
    }

    if (if_binary == 0) {
	printf("ascii noreply:\n");
	
	for (i = 0; i < request_number; ++i) {
	    send(sock, get_ascii_noreply, 	request_size, 	0);
	    send(sock, add_ascii_noreply, 	request_size, 	0);
	    send(sock, set_ascii_noreply, 	request_size, 	0);
	    send(sock, replace_ascii_noreply, request_size, 	0);
	    send(sock, append_ascii_noreply, 	request_size, 	0);
	    send(sock, prepend_ascii_noreply, request_size, 	0);
	    send(sock, incr_ascii_noreply, 	request_size, 	0);
	    send(sock, decr_ascii_noreply, 	request_size, 	0);
	    send(sock, delete_ascii_noreply, 	request_size, 	0);
	}
	
	clock_gettime(CLOCK_REALTIME, &finish);
	printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
	
	printf("\nascii reply:\n");
	clock_gettime(CLOCK_REALTIME, &start);
	
	for (i = 0; i < request_number; ++i) {
	    send(sock, get_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, add_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, set_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, replace_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, append_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, prepend_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, incr_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, decr_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, delete_ascii_reply, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);
	}
	
	clock_gettime(CLOCK_REALTIME, &finish);
	printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

    } else {

	printf("bin reply:\n");
	clock_gettime(CLOCK_REALTIME, &start);

	for (i = 0; i < request_number; ++i){
	    send(sock, get_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, add_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, set_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, replace_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, append_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, prepend_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, incr_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, decr_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);

	    send(sock, delete_bin, 	request_size, 	0);
	    recv(sock, recv_buff, BUFF_SIZE, 0);
	}

	clock_gettime(CLOCK_REALTIME, &finish);
	printf("Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec +
		(double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    }

    return NULL;
}

int init_socket_resources(void)
{
    struct sockaddr_in addr;
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
	printf("Alloc socket fail!\n");
	return 0;
    }

    return 1;
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
		break;
            default:
                assert(0);
        }
    }

    if (if_binary == 1)
    {
	if (request_size < BIN_MIX_REQUEST)
	{
	    printf("request_size is smaller than BIN_ASCII_REQUEST.\n");
	    return 0;
	}
    } else {
	if (request_size < ASCII_MIX_REQUEST)
	{
	    printf("request_size is smaller than ASCII_MIX_REQUEST.\n");
	    return 0;
	}
    }
    
    if (request_size > MEMCACHED_MAX_REQUEST)
    {
	printf("request_size is larger than MEMCACHED_MAX_REQUEST.\n");
	return 0;
    }

    if (init_socket_resources() == 0) {
	printf("socket fail\n");
	return 0;
    }
    init_message(if_binary);

    struct timespec start,
                    finish;
    clock_gettime(CLOCK_REALTIME, &start);

    test_with_regmem(if_binary);

    clock_gettime(CLOCK_REALTIME, &finish);

    printf("Total cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));
    return 0;
}

