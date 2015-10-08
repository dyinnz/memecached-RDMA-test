#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include <unistd.h>

#include <sys/types.h>
#include <arpa/inet.h>

#include "build_cmd.h"
#include "protocol_binary.h"

#define bool int
#define true (1)
#define false (0)

int 	request_size = 100;

/*******************************************************************************
 * Ascii message
 *
 ******************************************************************************/

char 	*add_ascii_noreply;
char 	*set_ascii_noreply;
char 	*replace_ascii_noreply;
char 	*append_ascii_noreply;
char 	*prepend_ascii_noreply;
char 	*incr_ascii_noreply;
char 	*decr_ascii_noreply;
char 	*delete_ascii_noreply;

char 	*get_ascii_reply;
char 	*add_ascii_reply;
char 	*set_ascii_reply;
char 	*replace_ascii_reply;
char 	*append_ascii_reply;
char 	*prepend_ascii_reply;
char 	*incr_ascii_reply;
char 	*decr_ascii_reply;
char 	*delete_ascii_reply;

int add_ascii_noreply_len;
int set_ascii_noreply_len;
int replace_ascii_noreply_len;
int append_ascii_noreply_len;
int prepend_ascii_noreply_len;
int incr_ascii_noreply_len;
int decr_ascii_noreply_len;
int delete_ascii_noreply_len;

int get_ascii_reply_len;
int add_ascii_reply_len;
int set_ascii_reply_len;
int replace_ascii_reply_len;
int append_ascii_reply_len;
int prepend_ascii_reply_len;
int incr_ascii_reply_len;
int decr_ascii_reply_len;
int delete_ascii_reply_len;

/******************************************************************************
 * Bin message
 * ****************************************************************************/
void 	*get_bin;
void 	*add_bin;
void 	*set_bin;
void 	*replace_bin;
void 	*append_bin;
void 	*prepend_bin;
void 	*incr_bin;
void 	*decr_bin;
void 	*delete_bin;

int get_bin_len;
int add_bin_len;
int set_bin_len;
int replace_bin_len;
int append_bin_len;
int prepend_bin_len;
int incr_bin_len;
int decr_bin_len;
int delete_bin_len;


static void alloc_message_space(int if_binary)
{
    if (if_binary == 0) {
	add_ascii_noreply = malloc(request_size);
	set_ascii_noreply = malloc(request_size);
	replace_ascii_noreply = malloc(request_size);
	append_ascii_noreply = malloc(request_size);
	prepend_ascii_noreply = malloc(request_size);
	incr_ascii_noreply = malloc(request_size);
	decr_ascii_noreply = malloc(request_size);
	delete_ascii_noreply = malloc(request_size);
	
	get_ascii_reply = malloc(request_size);
	add_ascii_reply = malloc(request_size);
	set_ascii_reply = malloc(request_size);
	replace_ascii_reply = malloc(request_size);
	append_ascii_reply = malloc(request_size);
	prepend_ascii_reply = malloc(request_size);
	incr_ascii_reply = malloc(request_size);
	decr_ascii_reply = malloc(request_size);
	delete_ascii_reply = malloc(request_size);
    } else {
	get_bin = (void *)malloc(request_size);
	add_bin = (void *)malloc(request_size);
	set_bin = (void *)malloc(request_size);
	replace_bin = (void *)malloc(request_size);
	append_bin = (void *)malloc(request_size);
	prepend_bin = (void *)malloc(request_size);
	incr_bin = (void *)malloc(request_size);
	decr_bin = (void *)malloc(request_size);
	delete_bin = (void *)malloc(request_size);
    }

    return;
}


static void write_to_buff(void **buff, void *data, int size)
{
    memcpy(*buff, data, size);
    *buff += size;
}


/**************************************************************************
 * Build ascii cmd
 *
 * ***********************************************************************/
static void build_ascii_cmd(char *cmd_cache, char *cmd_name, int cmd_length, bool if_extra, bool if_delta, bool if_reply, int* request_len)
{
    int keylen, bodylen, i;
    char *cache_header = cmd_cache;

    if (if_extra == true) // add set replace append prepend
	keylen = request_size - cmd_length - 12; // useless charactor
    else if (if_delta == true) // incr decr
	keylen = request_size - cmd_length - 4; // useless charactor
    else // delete
	keylen = request_size - cmd_length - 3; // useless cahractor

    if (keylen > 250) {
	bodylen = keylen - 250;
	keylen = 250;
    } else {
	bodylen = 1;
	keylen -= 1;
    }

    if (if_reply == false)
	keylen -= 8;

    if (strcmp(cmd_name, "incr") || strcmp(cmd_name, "decr"))
	bodylen = 1;
	
    write_to_buff((void**)&cmd_cache, cmd_name, cmd_length);

    write_to_buff((void**)&cmd_cache, " ", 1);
    for (i = 0; i < keylen; i++)
	write_to_buff((void**)&cmd_cache, "1", 1);
	
    if (if_extra == true) // add set replace append prepend
	write_to_buff((void**)&cmd_cache, " 0 0 1", 6);

    if (if_delta == true) {// incr decr
	write_to_buff((void**)&cmd_cache, " ", 1);
	for (i = 0; i < bodylen; i++)
	    write_to_buff((void**)&cmd_cache, "1", 1);
    }
	    
    if (if_reply == false)
	write_to_buff((void**)&cmd_cache, " noreply", 8);
	
    write_to_buff((void**)&cmd_cache, "\r\n", 2);
    
    if (if_extra == true) { // add set replace append prepend
	for (i = 1; i < keylen; i++)
	    write_to_buff((void**)&cmd_cache, "1", 1);
	write_to_buff((void**)&cmd_cache, "\r\n", 2);
    }

    *request_len = cmd_cache - cache_header;

    return;
}

static void init_ascii_message(void)
{
    build_ascii_cmd( 	add_ascii_noreply, 	"add", 		3, 	true, 	false, 	false, &add_ascii_noreply_len);
    build_ascii_cmd( 	set_ascii_noreply, 	"set", 		3, 	true, 	false, 	false, &set_ascii_noreply_len);
    build_ascii_cmd( 	replace_ascii_noreply, 	"replace", 	7, 	true, 	false, 	false, &replace_ascii_noreply_len);
    build_ascii_cmd( 	append_ascii_noreply, 	"append", 	6, 	true, 	false, 	false, &append_ascii_noreply_len);
    build_ascii_cmd( 	prepend_ascii_noreply, 	"prepend", 	7, 	true, 	false, 	false, &prepend_ascii_noreply_len);
    build_ascii_cmd( 	incr_ascii_noreply, 	"incr", 	4, 	false, 	true, 	false, &incr_ascii_noreply_len);
    build_ascii_cmd( 	decr_ascii_noreply, 	"decr", 	4, 	false, 	true, 	false, &decr_ascii_noreply_len);
    build_ascii_cmd( 	delete_ascii_noreply, 	"delete", 	6, 	false, 	false, 	false, &delete_ascii_noreply_len);

    build_ascii_cmd( 	get_ascii_reply, 	"get", 		3, 	false, 	false, 	true, &get_ascii_reply_len);
    build_ascii_cmd( 	add_ascii_reply, 	"add", 		3, 	true, 	false, 	true, &add_ascii_reply_len);
    build_ascii_cmd( 	set_ascii_reply, 	"set", 		3, 	true, 	false, 	true, &set_ascii_reply_len);
    build_ascii_cmd( 	replace_ascii_reply, 	"replace", 	7, 	true, 	false, 	true, &replace_ascii_reply_len);
    build_ascii_cmd( 	append_ascii_reply, 	"append", 	6, 	true, 	false, 	true, &append_ascii_reply_len);
    build_ascii_cmd( 	prepend_ascii_reply, 	"prepend", 	7, 	true, 	false, 	true, &prepend_ascii_reply_len);
    build_ascii_cmd( 	incr_ascii_reply, 	"incr", 	4, 	false, 	true, 	true, &incr_ascii_reply_len);
    build_ascii_cmd( 	decr_ascii_reply, 	"decr", 	4, 	false, 	true, 	true, &decr_ascii_reply_len);
    build_ascii_cmd( 	delete_ascii_reply, 	"delete", 	6, 	false, 	false, 	true, &delete_ascii_reply_len);
}


/******************************************************************************************
 * Build binary cmd
 *
 * ***************************************************************************************/
static void build_bin_cmd(void *cmd_cache, protocol_binary_command cmd, int* request_len)
{
    int keylen, valuelen, i;
    protocol_binary_request_header *tmp_hd;
    void *body_ptr; // point to the position after the header
    void *cache_header = cmd_cache;
    const int HEADER_LENGTH = 24;

    tmp_hd = (protocol_binary_request_header *)cmd_cache;

    switch (cmd) {
	case PROTOCOL_BINARY_CMD_GET:
	    tmp_hd->request.extlen = 0;

	    keylen = request_size - HEADER_LENGTH - tmp_hd->request.extlen; // for the reason of memory align, do not use sizeof(protocol_binary_request_header)!!!!!
	    body_ptr = cmd_cache + HEADER_LENGTH + tmp_hd ->request.extlen;
	    
	    keylen = keylen > 250 ? 250 : keylen;
	    valuelen = 0;

	    break;
	case PROTOCOL_BINARY_CMD_ADD:
	case PROTOCOL_BINARY_CMD_SET:
	case PROTOCOL_BINARY_CMD_REPLACE:
	    tmp_hd->request.extlen = 8;

	    keylen = request_size - HEADER_LENGTH - tmp_hd->request.extlen; // see above
	    body_ptr = cmd_cache + HEADER_LENGTH + tmp_hd->request.extlen;
	    ((protocol_binary_request_set *)tmp_hd)->message.body.flags = 0;
	    ((protocol_binary_request_set *)tmp_hd)->message.body.expiration = 0;

	    if (keylen > 250)
		valuelen = keylen - 250;
	    else
		valuelen = 1;
	    keylen -= valuelen;

	    break;
	case PROTOCOL_BINARY_CMD_APPEND:
	case PROTOCOL_BINARY_CMD_PREPEND:
	case PROTOCOL_BINARY_CMD_DELETE:
	    tmp_hd->request.extlen = 0;

	    keylen = request_size - HEADER_LENGTH - tmp_hd->request.extlen; // see above
	    body_ptr = cmd_cache + HEADER_LENGTH + tmp_hd->request.extlen;

	    if (cmd == PROTOCOL_BINARY_CMD_DELETE) {
		keylen = keylen > 250 ? 250 : keylen;
		valuelen = 0;
	    } else {
		if (keylen > 250)
		    valuelen = keylen - 250;
		else
		    valuelen = 1;
		keylen -= valuelen;
	    }

	    break;
	case PROTOCOL_BINARY_CMD_INCREMENT:
	case PROTOCOL_BINARY_CMD_DECREMENT:
	    tmp_hd->request.extlen = 20;

	    keylen = request_size - HEADER_LENGTH - tmp_hd->request.extlen; // see above
	    body_ptr = cmd_cache + HEADER_LENGTH + tmp_hd->request.extlen;
	    ((protocol_binary_request_incr *)tmp_hd)->message.body.delta = 1;
	    ((protocol_binary_request_incr *)tmp_hd)->message.body.initial = 0;
	    ((protocol_binary_request_incr *)tmp_hd)->message.body.expiration = 0;

	    keylen = keylen > 250 ? 250 : keylen;
	    valuelen = 0;
	    break;
	default:
	    assert(0);
    }

    tmp_hd->request.magic = PROTOCOL_BINARY_REQ;
    tmp_hd->request.opcode = cmd;
    tmp_hd->request.keylen = htons(keylen);
    tmp_hd->request.datatype = PROTOCOL_BINARY_RAW_BYTES;
    tmp_hd->request.bodylen = htonl(tmp_hd->request.extlen + keylen + valuelen);
    tmp_hd->request.reserved = tmp_hd->request.opaque = tmp_hd->request.cas = 0;

    for (i = 0 ; i < keylen + valuelen; i++)
	write_to_buff(&body_ptr, "1", 1);

    *request_len = body_ptr - cache_header;

    return;
}

static void init_binary_message(void)
{
    build_bin_cmd( 	get_bin, 	PROTOCOL_BINARY_CMD_GET, 	&get_bin_len);
    build_bin_cmd( 	add_bin, 	PROTOCOL_BINARY_CMD_ADD, 	&add_bin_len);
    build_bin_cmd( 	set_bin, 	PROTOCOL_BINARY_CMD_SET, 	&set_bin_len);
    build_bin_cmd( 	replace_bin, 	PROTOCOL_BINARY_CMD_REPLACE, 	&replace_bin_len);
    build_bin_cmd( 	append_bin, 	PROTOCOL_BINARY_CMD_APPEND, 	&append_bin_len);
    build_bin_cmd( 	prepend_bin, 	PROTOCOL_BINARY_CMD_PREPEND, 	&prepend_bin_len);
    build_bin_cmd( 	incr_bin, 	PROTOCOL_BINARY_CMD_INCREMENT, 	&incr_bin_len);
    build_bin_cmd( 	decr_bin, 	PROTOCOL_BINARY_CMD_DECREMENT, 	&decr_bin_len);
    build_bin_cmd( 	delete_bin, 	PROTOCOL_BINARY_CMD_DELETE, 	&delete_bin_len);

    return;
}


void init_message(int if_binary)
{
    alloc_message_space(if_binary);

    if (if_binary == 0)
	init_ascii_message();
    else 
	init_binary_message();
}

