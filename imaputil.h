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

int retrieve_structure(FILE *outstream, mime_message_t *msg, int show_extension_data);
int retrieve_envelope(FILE *outstream, struct list *rfcheader);
int show_address_list(FILE *outstream, struct mime_record *mr);
int show_mime_parameter_list(FILE *outstream, struct mime_record *mr, 
			     int force_subtype,
			     int only_extension);

mime_message_t* get_part_by_num(mime_message_t *msg, const char *part);

long rfcheader_dump(FILE *outstream, struct list *rfcheader, char **fieldnames, int nfields,
		    int equal_type);
long mimeheader_dump(FILE *outstream, struct list *mimeheader);

int haystack_find(int haystacklen, char **haystack, const char *needle);

int check_state_and_args(const char *command, const char *tag, char **args, 
			 int nargs, int state, ClientInfo *ci);
int next_fetch_item(char **args, int idx, fetch_items_t *fi);
int is_textplain(struct list *hdr);

char *date_sql2imap(char *sqldate);

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
