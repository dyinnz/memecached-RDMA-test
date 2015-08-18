CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: server sync-server

server: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

sync-server: sync-server.c
	gcc sync-server.c -o sync-server ${CFLAGS} ${LDFLAGS} -pg

