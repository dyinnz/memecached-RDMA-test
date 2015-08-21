CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: poll-server sync-server libevent-server

poll-server: poll-server.c
	gcc poll-server.c -o poll-server ${CFLAGS} ${LDFLAGS}

sync-server: sync-server.c
	gcc sync-server.c -o sync-server ${CFLAGS} ${LDFLAGS}

libevent-server: libevent-server.c
	gcc libevent-server.c -o libevent-server ${CFLAGS} ${LDFLAGS}
