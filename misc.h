/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef MISC_H_
#define MISC_H_
#include <grp.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include "db.h"
#include "debug.h"
#include "list.h"

int drop_priviledges (char *newuser, char *newgroup);

#endif
