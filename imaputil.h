/*
 * imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

int stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
void clarify_data(char *str);
char **build_args_array(const char *s);

#endif
