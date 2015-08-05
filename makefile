CFLAGS 	:= -Wall -g
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent

main: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

