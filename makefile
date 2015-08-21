CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: poll-server libevent-server client-test

poll-server: poll-server.c
	gcc poll-server.c -o poll-server ${CFLAGS} ${LDFLAGS}

libevent-server: libevent-server.c
	gcc libevent-server.c -o libevent-server ${CFLAGS} ${LDFLAGS}

client-test: client-test.c
	gcc client-test.c -o client-test ${CFLAGS} ${LDFLAGS}
