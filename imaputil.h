/*
 * imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

#include "serverservice.h"

int check_state_and_args(const char *command, const char *tag, const char **args, 
			 int nargs, int state, ClientInfo *ci);
int stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
void clarify_data(char *str);
char **build_args_array(const char *s);
void base64encode(char *in,char *out);
void base64decode(char *in,char *out);

#endif
