CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

all: libevent-server client-test async-client

libevent-server: libevent-server.c
	gcc libevent-server.c -o libevent-server ${CFLAGS} ${LDFLAGS}

client-test: client-test.c
	gcc client-test.c -o client-test ${CFLAGS} ${LDFLAGS}

async-client: async-client.c hashtable.c
	gcc hashtable.c ${CFLAGS} -c
	gcc async-client.c ${CFLAGS} ${LDFLAGS} -c
	gcc async-client.o hashtable.o -o async-client ${CFLAGS} ${LDFLAGS}

clean:
	rm *.o
