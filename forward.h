/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _FORWARD_H
#define _FORWARD_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define FW_SENDMAIL SENDMAIL

int pipe_forward(FILE *instream, struct list *targets, char *from,  char *header, unsigned long databasemessageid);

#endif
