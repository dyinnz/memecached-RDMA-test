#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>

#define MAX_SIZE (1048576 + 256)

static int request_number = 10000;
static const char *pstr_server = "127.0.0.1";
static const char *pstr_port = "11211";
static int buff_size = 1024 * 16;
static int verbose = 0;
static char sock_buff[MAX_SIZE] = {0};

int build_connect(const char *server, const char *port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket()");
        return -1;
    }

    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(atoi(port));

    if (inet_pton(AF_INET, pstr_server, &servaddr.sin_addr) < 0) {
        perror("inet_pton()");
        return -1;
    }

    if (0 != connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr))) {
        perror("connect()");
        return -1;
    }

    return sockfd;
}

static char get_msg[] = "get foo\r\n";
static char delete_msg[] = "delete foo\r\n";

void test_speed(int sockfd, int memory_size, int request_number) {
    static char add_msg[MAX_SIZE];
    bzero(add_msg, MAX_SIZE);
    snprintf(add_msg, MAX_SIZE, "add foo 0 0 %d\r\nhello", memory_size);
    printf("add_msg:\n%s\n", add_msg);

    size_t pos = strstr(add_msg, "\r\n") - add_msg + 2;
    add_msg[pos + memory_size] = '\r';
    add_msg[pos + memory_size+1] = '\n';
    size_t total_size = pos + memory_size + 2;
    printf("total_size: %zd\n", total_size);

    int i = 0;
    for (i = 0; i < request_number; ++i) {
        write(sockfd, add_msg, total_size);
        read(sockfd, sock_buff, MAX_SIZE);
        if (verbose) {
            printf("Recv:\n%s\n", sock_buff);
        }
        
        write(sockfd, get_msg, sizeof(get_msg)-1);
        read(sockfd, sock_buff, MAX_SIZE);
        if (verbose) {
            printf("Recv:\n%s\n", sock_buff);
        }

        write(sockfd, delete_msg, sizeof(delete_msg)-1);
        read(sockfd, sock_buff, MAX_SIZE);
        if (verbose) {
            printf("Recv:\n%s\n", sock_buff);
        }
    }
}

int main(int argc, char *argv[]) {
    char c = '\0';
    while (-1 != (c = getopt(argc, argv,
            "r:"
            "p:"
            "s:"
            "m:"
            "v"
    ))) {
        switch (c) {
            case 'r':
                request_number = atoi(optarg);
                break;
            case 'p':
                pstr_port = optarg;
                break;
            case 's':
                pstr_server = optarg;
                break;
            case 'm':
                buff_size = atoi(optarg);
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                assert(0);
        }
    }
    struct timespec start,
                    finish;
    clock_gettime(CLOCK_REALTIME, &start);

    int sockfd = build_connect(pstr_server, pstr_port);
    if (sockfd < 0) {
        fprintf(stderr, "connect error!\n");
        return -1;
    }
    test_speed(sockfd, buff_size, request_number);

    clock_gettime(CLOCK_REALTIME, &finish);
    printf("MAIN Cost time: %lf secs\n", (double)(finish.tv_sec-start.tv_sec + 
                (double)(finish.tv_nsec - start.tv_nsec)/1000000000 ));

    return 0;
}
