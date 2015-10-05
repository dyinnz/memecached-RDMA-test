CFLAGS 	:= -Wall -O2
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent -lrt

all: libevent-server client-test async-client tcp-client client-socket client-bin

libevent-server: libevent-server.c
	gcc libevent-server.c -o libevent-server ${CFLAGS} ${LDFLAGS}

client-test: client-test.c
	gcc client-test.c -o client-test ${CFLAGS} ${LDFLAGS}

async-client: async-client.c hashtable.c
	gcc hashtable.c ${CFLAGS} -c
	gcc async-client.c ${CFLAGS} ${LDFLAGS} -c
	gcc async-client.o hashtable.o -o async-client ${CFLAGS} ${LDFLAGS}

tcp-client: tcp-client.c
	gcc tcp-client.c -o tcp-client ${CFLAGS} ${LDFLAGS}

client-socket: client-socket.c build_cmd.c
	gcc client-socket.c build_cmd.c -o client-socket ${CFLAGS} -lrt
#	gcc client-socket.c build_cmd.c -o client-socket ${CFLAGS} ${LDFLAGS}

client-rdma: client-rdma.c build_cmd.c
	gcc client-rdma.c build_cmd.c -o client-rdma ${CFLAGS} -lrdmacm -libverbs -lrt 
#	gcc client-rdma.c build_cmd.c -o client-rdma ${CFLAGS} ${LDFLAGS}

clean:
	rm *.o
