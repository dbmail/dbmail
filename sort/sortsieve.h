/* $Id$ 
 * (c) 2003 Aaron Stone
 *
 * Headers for sieve.c */

#ifndef _SIEVE_H
#define _SIEVE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "sort.h"
#include "dbmailtypes.h"
#include <sieve2_interface.h>

#define MAX_SIEVE_SCRIPTNAME 100

int sortsieve_msgsort(u64_t useridnr, char *header, u64_t headersize,
		      u64_t messagesize, struct list *actions);
int sortsieve_unroll_action(sieve2_action_t * a, struct list *actions);
int sortsieve_script_validate(char *script, char **errmsg);

#endif
