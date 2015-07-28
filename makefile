CFLAGS 	:= -Wall -g
LD 		:= gcc
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent -I../libevent/include -L../libevent/lib

main: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

