/* $Id$ 
 * (c) 2003 Aaron Stone
 *
 * Headers for sorting.c */

#ifndef _SORTING_H
#define _SORTING_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "dsn.h"
#include "debug.h"
#include "dbmailtypes.h"

#define SA_KEEP		1
#define SA_DISCARD	2
#define SA_REDIRECT	3
#define SA_REJECT	4
#define SA_FILEINTO	5
#define SA_SIEVE	6

typedef struct sort_action {
	int method;
	char *destination;
	char *message;
} sort_action_t;

dsn_class_t sort_and_deliver(u64_t msgidnr,
			     const char *header, u64_t headersize,
			     u64_t msgsize, u64_t rfcsize,
			     u64_t useridnr, const char *mailbox);

#endif				/* #ifndef _SORTING_H */
