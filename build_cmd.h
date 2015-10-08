#ifndef _BUILD_CMD_
#define _BUILD_CMD_

#include <stdint.h>

/***************************************************************************//**
 * Ascii message
 *
 ******************************************************************************/

extern char 	*add_ascii_noreply;
extern char 	*set_ascii_noreply;
extern char 	*replace_ascii_noreply;
extern char 	*append_ascii_noreply;
extern char 	*prepend_ascii_noreply;
extern char 	*incr_ascii_noreply;
extern char 	*decr_ascii_noreply;
extern char 	*delete_ascii_noreply;

extern char 	*get_ascii_reply;
extern char 	*add_ascii_reply;
extern char 	*set_ascii_reply;
extern char 	*replace_ascii_reply;
extern char 	*append_ascii_reply;
extern char 	*prepend_ascii_reply;
extern char 	*incr_ascii_reply;
extern char 	*decr_ascii_reply;
extern char 	*delete_ascii_reply;

extern int add_ascii_noreply_len;
extern int set_ascii_noreply_len;
extern int replace_ascii_noreply_len;
extern int append_ascii_noreply_len;
extern int prepend_ascii_noreply_len;
extern int incr_ascii_noreply_len;
extern int decr_ascii_noreply_len;
extern int delete_ascii_noreply_len;

extern int get_ascii_reply_len;
extern int add_ascii_reply_len;
extern int replace_ascii_reply_len;
extern int append_ascii_reply_len;
extern int prepend_ascii_reply_len;
extern int incr_ascii_reply_len;
extern int decr_ascii_reply_len;
extern int delete_ascii_reply_len;

/******************************************************************************
 * Bin message
 * ****************************************************************************/
extern void 	*get_bin;
extern void 	*add_bin;
extern void 	*set_bin;
extern void 	*replace_bin;
extern void 	*append_bin;
extern void 	*prepend_bin;
extern void 	*incr_bin;
extern void 	*decr_bin;
extern void 	*delete_bin;

extern int get_bin_len;
extern int add_bin_len;
extern int set_bin_len;
extern int replace_bin_len;
extern int append_bin_len;
extern int prepend_bin_len;
extern int incr_bin_len;
extern int decr_bin_len;
extern int delete_bin_len;


#define ASCII_MIX_REQUEST (28)
#define BINARY_MIX_REQUEST (45)
#define MEMCACHED_MAX_REQUEST (1024*1024)

extern void init_message(int if_binary);

extern int request_size;

#endif
