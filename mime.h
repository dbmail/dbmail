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
 */

#ifndef _MIME_H
#define _MIME_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"

#define MEM_BLOCK 1024

struct mime_record {
/* if these are to be changed to ptrs, the following has to be updated:
   mime.c (duh)
   db_parse_as_text
   a cleanup for all the memory allocated 
*/
	char field[MIME_FIELD_MAX];
	char value[MIME_VALUE_MAX];
};

void mime_findfield(const char *fname, struct list *mimelist,
		    struct mime_record **mr);
int mail_adr_list(char *scan_for_field, struct list *targetlist,
		  struct list *mimelist);
int mime_readheader(const char *datablock, u64_t * blkidx, 
		    struct list *mimelist, u64_t * headersize);

#endif
