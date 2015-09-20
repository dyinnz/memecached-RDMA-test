#! /usr/bin/env python

import socket
import sys, getopt
import time

g_server = "127.0.0.1"
g_port = 11211
g_request = 1000
g_memory_size = 1024
g_verbose = False

def build_conn(server, port):
    conn = socket.socket()
    try:
        conn.connect((server, port))
    except Exception, e:
        print e
        return None

    print 'connect ok'
    return conn

def generate_msg(memory_size):
    add_msg = 'add foo 0 0 ' + str(memory_size) + '\r\n'
    add_msg += 'hello' + '\0'*(memory_size - 5) + '\r\n'
    return add_msg

def measure_time(func):
    def _func(*args, **kwargs):
        start = time.time()
        ret = func(*args, **kwargs)
        end = time.time()
        print end - start
        return ret
    return _func

@measure_time
def test_client(conn, add_msg, request):
    INFO_SIZE = 512
    for i in range(request):
        conn.send(add_msg)
        ret = conn.recv(INFO_SIZE)
        if g_verbose:
            print ret

        conn.send('get foo\r\n')
        ret = conn.recv(len(add_msg))
        if g_verbose:
            print ret

        conn.send('delete foo\r\n')
        ret = conn.recv(INFO_SIZE)
        if g_verbose:
            print ret

if __name__ == '__main__':
    opts, args = getopt.getopt(sys.argv[1:], 's:p:r:m:v')
    for opt, value in opts:
        if '-s' == opt:
            g_server = value
        elif '-p' == opt:
            g_port = int(value)
        elif '-r' == opt:
            g_request = int(value)
        elif '-m' == opt:
            g_memory_size = int(value)
            if g_memory_size < 5:
                g_memory_size = 5
        elif '-v' == opt:
            g_verbose = True

    msg = generate_msg(g_memory_size)
    conn = build_conn(g_server, g_port)
    test_client(conn, msg, g_request)




