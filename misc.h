/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _MISC_H
#define _MISC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <grp.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include "db.h"
#include "debug.h"
#include "list.h"

int drop_priviledges (char *newuser, char *newgroup);
char * itoa (int i);
void create_unique_id(char *target, u64_t messageid);

#endif
