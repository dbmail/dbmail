/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _HEADER_H
#define _HEADER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

int read_header(FILE *instream, u64_t *newlines, u64_t *headersize, char **header);
int read_header_process(FILE *instream, struct list *userids,
		struct list *bounces, struct list *fwds,
		const char *field, char *hdrdata,
		u64_t *newlines, char **bounce_path);
int add_address(const char *address, struct list *userids,
		struct list *bounces, struct list *fwds);
int add_username(const char *uname, struct list *userids);

#endif
