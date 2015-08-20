CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: server sync-server sync-client-test

server: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

sync-server: sync-server.c
	gcc sync-server.c -o sync-server ${CFLAGS} ${LDFLAGS}

sync-client-test: sync-client-test.c
	gcc sync-client-test.c -o sync-client-test ${CFLAGS} ${LDFLAGS}
