/*
 * imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

#include "serverservice.h"
#include "imap4.h"
#include "dbmysql.h"
#include <stdio.h>

int retrieve_structure(FILE *outstream, mime_message_t *msg);
int retrieve_envelope(FILE *outstream, struct list *rfcheader);
int show_mime_parameter_list(FILE *outstream, struct mime_record *mr, int nil_for_firstval_missing);

mime_message_t* get_part_by_num(mime_message_t *msg, const char *part);

int rfcheader_dump(FILE *outstream, struct list *rfcheader, char **fieldnames, int nfields,
		   int offset, int cnt, int equal_type);
int haystack_find(int haystacklen, char **haystack, const char *needle);

int check_state_and_args(const char *command, const char *tag, char **args, 
			 int nargs, int state, ClientInfo *ci);
int get_fetch_items(char **args, fetch_items_t *fi);
int stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
int checkmailboxname(const char *s);
void clarify_data(char *str);
char **build_args_array(const char *s);
void base64encode(char *in,char *out);
void base64decode(char *in,char *out);
char **give_chunks(const char *str, char delimiter);
void free_chunks(char **chunks);
int binary_search(const unsigned long *array, int arraysize, unsigned long key);

#endif
