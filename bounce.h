/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * Headers for bounce.c */

#ifndef _BOUNCE_H
#define _BOUNCE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "debug.h"

#define BOUNCE_NO_SUCH_USER 1
#define BOUNCE_STORAGE_LIMIT_REACHED 2
#define BOUNCE_INVALID_MAIL_FORMAT 3

int bounce (char *header, unsigned long headersize, char *destination_address, int type);

#endif
