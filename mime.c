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
 * Functions for parsing a mime mailheader (actually just for scanning for email messages
	and parsing the messageID */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mime.h"
#include "debug.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dbmail-message.h"

/* extern char *header; */
/* extern u64_t headersize; */

/* extern struct list mimelist;  */
/* extern struct list users; */

/* checks if s points to the end of a header. */
static int is_end_of_header(const char *s);


/* mime_fetch_headers()
 *
 * same as mime_headerheader, except it doesn't determine the start-index of the
 * body, nor the total size of the headers. Those values are only used by rfcmsg.c
 * 
 * 
 */
static void _register_header(const char *field, const char *value, gpointer mimelist);
static void _register_header(const char *field, const char *value, gpointer mimelist)
{
	struct mime_record *mr = g_new0(struct mime_record, 1);
	g_strlcpy(mr->field, field, MIME_FIELD_MAX);
	g_strlcpy(mr->value, value, MIME_VALUE_MAX);
	list_nodeadd((struct list *)mimelist, mr, sizeof(*mr));
	g_free(mr);
}

int mime_fetch_headers(const char *datablock, struct list *mimelist) 
{
	GString *raw = g_string_new(datablock);
	struct DbmailMessage *m = dbmail_message_new();
	GMimeMessage *message;
	m = dbmail_message_init_with_string(m, raw);
	g_mime_header_foreach(m->content->headers, _register_header, (gpointer)mimelist);
	g_string_free(raw,1);

	if (dbmail_message_get_class(m) == DBMAIL_MESSAGE && GMIME_MESSAGE(m->content)->mime_part) {
		message = (GMimeMessage *)(m->content);
		g_mime_header_foreach(GMIME_OBJECT(message->mime_part)->headers, _register_header, (gpointer)mimelist);
	}

	return 0;	
}
	
	

/* 
 * mime_readheader()
 *
 * same as mime_list() but adds the number of bytes read to blkidx
 * and returns the number of newlines passed
 *
 * headersize will be set to the actual amount of bytes used to store the header:
 * field/value strlen()'s plus 4 bytes for each headeritem: ': ' (field/value
 * separator) and '\r\n' to end the line.
 *
 * newlines within value will be expanded to '\r\n'
 *
 * if blkdata[0] == \n no header is expected and the function will return immediately
 * (headersize 0)
 *
 * returns -1 on parse failure, -2 on memory error; number of newlines on succes
 */


int mime_readheader(const char *datablock, u64_t * blkidx, struct list *mimelist,
		    u64_t * headersize)
{
	int valid_mime_lines = 0, idx, totallines = 0, j;
	int fieldlen, vallen;
	char *endptr, *startptr, *delimiter;
	char *blkdata;
	int cr_nl_present;	/* 1 if a '\r\n' is found */

	char field[MIME_FIELD_MAX];
	
	/* moved header-parsing to separate function */
	mime_fetch_headers(datablock, mimelist);
	
	blkdata = g_strdup(datablock);

	*headersize = 0;

	if (blkdata[0] == '\n') {
		trace(TRACE_DEBUG, "%s,%s: found an empty header", __FILE__, __func__);
		(*blkidx)++;	/* skip \n */
		g_free(blkdata);
		return 1;	/* found 1 newline */
	}

	startptr = blkdata;
	while (*startptr) {
		cr_nl_present = 0;
		/* quick hack to jump over those naughty \n\t fields */
		endptr = startptr;
		while (*endptr) {
			/* field-ending: \n + (non-white space) */
			if (*endptr == '\n') {
				totallines++;
				if (is_end_of_header(endptr))
					break;
			}
			if (*endptr == '\r' && *(endptr + 1) == '\n') {
				cr_nl_present = 1;
				totallines++;
				endptr++;
				if (is_end_of_header(endptr))
					break;
			}
			endptr++;
		}

		if (!(*endptr)) {
			/* end of data block reached (??) */
			*blkidx += (endptr - startptr);
			g_free(blkdata);
			return totallines;
		}

		/* endptr points to linebreak now */
		/* MIME field+value is string from startptr till endptr */
		if (cr_nl_present)
			*(endptr - 1) = '\0';
		*endptr = '\0';	/* replace newline to terminate string */

		/* parsing tmpstring for field and data */
		/* field is name:value */

		delimiter = strchr(startptr, ':');

		if (delimiter) {
			/* found ':' */
			valid_mime_lines++;
			*delimiter = '\0';	/* split up strings */

			/* skip all spaces and colons after the fieldname */
			idx = 1;
			while ((delimiter[idx] == ':') || (delimiter[idx] == ' '))
				idx++;

			/* &delimiter[idx] is field value, startptr is field name */
			fieldlen = snprintf(field, MIME_FIELD_MAX, "%s", startptr);
			for (vallen = 0, j = 0; delimiter[idx + j] && vallen < MIME_VALUE_MAX; j++, vallen++) {
				if (delimiter[idx + j] == '\n') 
					vallen++;
			}
			*headersize += strlen(field) + vallen + 4;	/* <field>: <value>\r\n --> four more */

			/* restore blkdata */
			*delimiter = ':';

		}
		
		if (cr_nl_present)
			*(endptr - 1) = '\r';
		*endptr = '\n';	/* restore blkdata */

		*blkidx += (endptr - startptr);
		(*blkidx)++;

		startptr = endptr + 1;	/* advance to next field */

		if (*startptr == '\n' || (*startptr == '\r' && *(startptr + 1) == '\n')) {
			/* end of header: double newline */
			totallines++;
			(*blkidx)++;
			(*headersize) += 2;
			g_free(blkdata);
			return totallines;
		}

	}

	/* everything down here should be unreachable */

	trace(TRACE_DEBUG, "%s,%s: mimeloop finished", __FILE__, __func__);
	if (valid_mime_lines < 2) {
		trace(TRACE_ERROR, "%s,%s: no valid mime headers found\n", __FILE__, __func__);
		g_free(blkdata);
		return -1;
	}

	g_free(blkdata);
	return totallines;
}

/*
 * mime_findfield()
 *
 * finds a MIME header field
 *
 */
void mime_findfield(const char *fname, struct list *mimelist,
		    struct mime_record **mr)
{
	struct element *current;

	current = list_getstart(mimelist);
	while (current) {
		*mr = current->data;	/* get field/value */
		if (strcasecmp((*mr)->field, fname) == 0)
			return;	/* found */
		current = current->nextnode;
	}
	*mr = NULL;
}


int mail_adr_list(char *scan_for_field, struct list *targetlist,
		  struct list *mimelist)
{
	struct element *raw;
	struct mime_record *mr;
	char *tmpvalue, *ptr, *tmp;

	if (!scan_for_field || !targetlist || !mimelist) {
		trace(TRACE_ERROR,
		      "mail_adr_list(): received a NULL argument\n");
		return -1;
	}

	trace(TRACE_DEBUG,
	      "mail_adr_list(): mimelist currently has [%ld] nodes",
	      mimelist->total_nodes);

	memtst((tmpvalue =
		(char *) my_calloc(MIME_VALUE_MAX, sizeof(char))) == NULL);

	trace(TRACE_INFO, "mail_adr_list(): mail address parser starting");

	raw = list_getstart(mimelist);
	trace(TRACE_DEBUG, "mail_adr_list(): total fields in header %ld",
	      mimelist->total_nodes);
	while (raw != NULL) {
		mr = (struct mime_record *) raw->data;
		trace(TRACE_DEBUG, "mail_adr_list(): scanning for %s",
		      scan_for_field);
		if ((strcasecmp(mr->field, scan_for_field) == 0)) {
			/* Scan for email addresses and add them to our list */
			/* the idea is to first find the first @ and go both ways */
			/* until an non-emailaddress character is found */
			ptr = strstr(mr->value, "@");
			while (ptr != NULL) {
				/* found an @! */
				/* first go as far left as possible */
				tmp = ptr;
				while ((tmp != mr->value) &&
				       (tmp[0] != '<') &&
				       (tmp[0] != ' ') &&
				       (tmp[0] != '\0') && (tmp[0] != ','))
					tmp--;
				if ((tmp[0] == '<') || (tmp[0] == ' ')
				    || (tmp[0] == '\0')
				    || (tmp[0] == ','))
					tmp++;
				while ((ptr != NULL) &&
				       (ptr[0] != '>') &&
				       (ptr[0] != ' ') &&
				       (ptr[0] != ',') && (ptr[0] != '\0'))
					ptr++;
				memtst((strncpy(tmpvalue, tmp, ptr - tmp))
				       == NULL);
				/* always set last value to \0 to end string */
				tmpvalue[ptr - tmp] = '\0';

				/* one extra for \0 in strlen */
				memtst((list_nodeadd(targetlist, tmpvalue,
						     (strlen(tmpvalue) +
						      1))) == NULL);

				/* printf ("total nodes:\n");
				   list_showlist(&targetlist);
				   next address */
				ptr = strstr(ptr, "@");
				trace(TRACE_DEBUG,
				      "mail_adr_list(): found %s, next in list is %s",
				      tmpvalue, ptr ? ptr : "<null>");
			}
		}
		raw = raw->nextnode;
	}

	my_free(tmpvalue);

	trace(TRACE_DEBUG, "mail_adr_list(): found %ld emailaddresses",
	      targetlist->total_nodes);

	trace(TRACE_INFO, "mail_adr_list(): mail address parser finished");

	if (targetlist->total_nodes == 0)	/* no addresses found */
		return -1;

	return 0;
}

/* return 1 if this is the end of a header. That is, it returns 1 if
 * the next character is a non-whitespace character, or a newline or
 * carriage return + newline. If the next character is a white space
 * character, but not a newline, or carriage return + newline, the
 * header continues.
 */
int is_end_of_header(const char *s)
{
	if (!isspace(s[1]))
		return 1;

	if (s[1] == '\n')
		return 1;

	if (s[1] == '\r' && s[2] == '\n')
		return 1;

	return 0;
}
