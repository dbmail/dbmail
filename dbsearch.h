/*
 * dbsearch.h
 *
 * function prototypes for search functionality
 *
 * should connect to any dbXXX.c file where XXXX is the dbase to be used
 */

#ifndef _DBSEARCH_H
#define _DBSEARCH_H

#include "dbmailtypes.h"

int db_search(int *rset, int setlen, const char *key, mailbox_t *mb);
int db_search_parsed(int *rset, int setlen, search_key_t *sk, mailbox_t *mb);

int db_search_messages(char **search_keys, u64_t **search_results, int *nsresults,
		       u64_t mboxid);

#endif
