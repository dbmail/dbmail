/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ 
 *
 * Functions to create md5 hash from buf */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbmd5.h"
#include "md5.h"
#include "debug.h"

unsigned char *makemd5(char *buf)
{
	struct GdmMD5Context mycontext;
	unsigned char result[16];
	unsigned char *md5hash;
	int i;

	md5hash = (unsigned char *) my_malloc(33);
	if (md5hash == NULL) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory",
		      __FILE__, __FUNCTION__);
		return NULL;
	}

	gdm_md5_init(&mycontext);
	gdm_md5_update(&mycontext, buf, strlen(buf));
	gdm_md5_final(result, &mycontext);

	for (i = 0; i < 16; i++) {
		sprintf(&md5hash[i * 2], "%02x", result[i]);
	}

	return md5hash;
}
