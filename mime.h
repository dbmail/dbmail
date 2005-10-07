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

/* $Id: mime.h 1893 2005-10-05 15:04:58Z paul $ 
 */

#ifndef _MIME_H
#define _MIME_H

#include "dbmail.h"

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

struct DbmailMessage * mime_fetch_headers(struct DbmailMessage *message, struct dm_list *mimelist);

int mime_readheader(struct DbmailMessage *message, struct dm_list *mimelist, u64_t * headersize);

void mime_findfield(const char *fname, struct dm_list *mimelist, struct mime_record **mr);

int mail_address_build_list(const char *scan_for_field, struct dm_list *targetlist, struct dm_list *mimelist);


#endif
