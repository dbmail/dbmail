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
 * Header.c implements functions to read an email header
 * and parse out certain goodies, such as deliver-to
 * fields and common fields for the fast header cache
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "list.h"
#include "auth.h"
#include "mime.h"
#include "header.h"
#include "db.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HEADER_BLOCK_SIZE 1024

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

/* Reads from the specified pipe until either a lone carriage
 * return or lone period stand on a line by themselves. The
 * number of non-\r\n newlines is recorded along the way.
 * The variable "header" should be passed by & reference,
 * and should be defined (duh) but not malloc'ed (honest)
 * before calling.
 *
 * The caller is responsible for free'ing header, even upon error.
 *
 * Return values:
 *   1 on success
 *   0 on failure
 * */
int read_header(FILE * instream, u64_t * headerrfcsize, u64_t * headersize,
		char **header)
{
	int tmpchar;
	char *tmpline;
	char *tmpheader;
	u64_t tmpheadersize = 0, tmpheaderrfcsize = 0;
	size_t usedmem = 0, linemem = 0;
	size_t allocated_blocks = 1;
	int myeof = 0;

	memtst((tmpheader =
		(char *) my_malloc(HEADER_BLOCK_SIZE)) == NULL);
	memtst((tmpline = (char *) my_malloc(MAX_LINE_SIZE)) == NULL);

	/* Resetting */
	memset(tmpline, '\0', MAX_LINE_SIZE);
	memset(tmpheader, '\0', HEADER_BLOCK_SIZE);

	/* here we will start a loop to read in the message header */
	/* the header will be everything up until \n\n or an EOF of */
	/* in_stream (instream) */

	trace(TRACE_INFO, "read_header(): readheader start");

	while (!feof(instream) && !myeof) {

		/* Read a char at a time until there's a full line in tmpline. */
		linemem = 0;
		for (;;) {
			if (linemem == MAX_LINE_SIZE)
				break;

			tmpchar = fgetc(instream);
			if (tmpchar == EOF)
				break;

			tmpline[linemem++] = tmpchar;

			/* Always break at the end of a line, incrementing rfcsize
			 * if said end of line is a \n-only end of line. */
			if (tmpchar == '\n') {
			    if (tmpline[linemem-1] != '\r') {
				tmpheaderrfcsize++;
			    }
			    break;
			}
		}
		tmpheadersize += linemem;
		tmpheaderrfcsize += linemem;

		/* fgetc return EOF for both EOF and ERR. Check it here! */
		if (ferror(instream)) {
			trace(TRACE_ERROR,
			      "read_header(): error on instream: [%s]",
			      strerror(errno));
			my_free(tmpline);
			my_free(tmpheader);
			/* NOTA BENE: Make sure that the caller knows to free
			 * the header block even if there's been an error! */
			return 0;
		}

		/* The end of the header could be \n\n, \r\n\r\n,
		 * or \r\n.\r\n, in the accidental case that we
		 * ate the whole SMTP message, too! */
		if (strncmp(tmpline, ".\r\n", (linemem < 3 ? linemem : 3)) == 0) {
			/* This is the end of the message! */
			trace(TRACE_DEBUG,
			      "read_header(): single period found");
			myeof = 1;
		} else if (strncmp(tmpline, "\n", (linemem < 1 ? linemem : 1)) == 0
			   || strncmp(tmpline, "\r\n", (linemem < 2 ? linemem : 2)) == 0) {
			/* We've found the end of the header */
			trace(TRACE_DEBUG,
			      "read_header(): single blank line found");
			myeof = 1;
		}

		/* Even if we hit the end of the header, don't forget to copy the extra
		 * returns. They will always be needed to separate the header from the
		 * message during any future retrieval of the fully concatenated message.
		 * */

		trace(TRACE_DEBUG,
		      "read_header(): copying line into header");

		/* If this happends it's a very big header */
		if (usedmem + linemem >
		    (allocated_blocks * HEADER_BLOCK_SIZE)) {
			/* Update block counter */
			allocated_blocks++;
			trace(TRACE_DEBUG,
			      "read_header(): mem current: [%zd] reallocated "
			      "to [%zd]",
			      usedmem, allocated_blocks * HEADER_BLOCK_SIZE);
			memtst((tmpheader =
				(char *) realloc(tmpheader,
						 allocated_blocks *
						 HEADER_BLOCK_SIZE)) ==
			       NULL);
		}

		/* This *should* always happen, but better safe than overflowing! */
		if (usedmem + linemem <
		    (allocated_blocks * HEADER_BLOCK_SIZE)) {
			/* Copy starting at the current usage offset */
			strncat(tmpheader, tmpline, linemem);
			usedmem += linemem;

			/* Resetting strlen for tmpline */
			tmpline[0] = '\0';
			linemem = 0;
		}
	}

	trace(TRACE_DEBUG, "read_header(): readheader done");
	trace(TRACE_DEBUG,
	      "read_header(): found header [%s] of len [%zd] using mem [%zd]",
	      tmpheader, strlen(tmpheader), usedmem);

	my_free(tmpline);

	if (usedmem == 0) {
		trace(TRACE_ERROR,
		      "read_header(): no valid mail header found\n");
		if (tmpheader)
			my_free(tmpheader);
		return 0;
	}

	/* Assign to the external variable */
	*header = tmpheader;
	*headersize = tmpheadersize;
	*headerrfcsize = tmpheaderrfcsize;

	/* The caller is responsible for freeing header/tmpheader. */

	trace(TRACE_INFO, "read_header(): function successfull\n");
	return 1;
}
