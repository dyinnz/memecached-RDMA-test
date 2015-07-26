event_server: event_server.c
	gcc event_server.c -o event_server -g -Wall -lxio -levent
