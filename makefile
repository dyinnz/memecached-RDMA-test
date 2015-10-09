CFLAGS 	:= -Wall -O2
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -lrt

all: client-test client-socket client-rdma 

client-test: client-test.c
	gcc client-test.c -o client-test ${CFLAGS} ${LDFLAGS}

client-socket: client-socket.c build_cmd.c
	gcc client-socket.c build_cmd.c -o client-socket ${CFLAGS} ${LDFLAGS}

client-rdma: client-rdma.c build_cmd.c
	gcc client-rdma.c build_cmd.c -o client-rdma ${CFLAGS} ${LDFLAGS} 

clean:
	rm *.o -f
	rm client-rdma client-socket client-test -f
