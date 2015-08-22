CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: poll-server libevent-server client-test xio_server xio_client

poll-server: poll-server.c
	gcc poll-server.c -o poll-server ${CFLAGS} ${LDFLAGS}

libevent-server: libevent-server.c
	gcc libevent-server.c -o libevent-server ${CFLAGS} ${LDFLAGS}

client-test: client-test.c
	gcc client-test.c -o client-test ${CFLAGS} ${LDFLAGS}

xio_server: xio_server.c
	gcc xio_server.c -o xio_server ${CFLAGS} -lxio -levent

xio_client: xio_client.c
	gcc xio_client.c -o xio_client ${CFLAGS} -lxio -levent
