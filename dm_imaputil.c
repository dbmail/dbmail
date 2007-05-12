/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* 
 * 
 * dm_imaputil.c
 *
 * IMAP-server utility functions implementations
 */


#include "dbmail.h"

#define THIS_MODULE "imap"


#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* consts */
const char AcceptedChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? \n\r";

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";
/*
 *
 */
size_t stridx(const char *s, char ch)
{
	size_t i;

	for (i = 0; s[i] && s[i] != ch; i++);

	return i;
}

/*
 * checktag()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checktag(const char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedTagChars, s[i])) {
			/* wrong char found */
			return 0;
		}
	}
	return 1;
}

/* unwrap strings */
int mime_unwrap(char *to, const char *from) 
{
	while (*from) {
		if (((*from == '\n') || (*from == '\r')) && isspace(*(from+1))) {
			from+=2;
			continue;
		}
		*to++=*from++;
	}
	*to='\0';
	return 0;
}




