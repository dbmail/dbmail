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

/* extern char *header; */
/* extern u64_t headersize; */

/* extern struct list mimelist;  */
/* extern struct list users; */

/* checks if s points to the end of a header. */
static int is_end_of_header(const char *s);
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
	size_t prevlen = 0, new_add = 1;
/*  u64_t saved_idx = *blkidx; only needed if we bail out on invalid data */

	char *endptr, *startptr, *delimiter;
	char *blkdata;
	struct mime_record *mr, *prev_mr = NULL;
	struct element *el = NULL;
	int cr_nl_present;	/* 1 if a '\r\n' is found */

	trace(TRACE_DEBUG, "mime_readheader(): entering mime loop");

	blkdata = my_strdup(datablock);

	list_init(mimelist);
	*headersize = 0;

	if (blkdata[0] == '\n') {
		trace(TRACE_DEBUG,
		      "mime_readheader(): found an empty header\n");
		(*blkidx)++;	/* skip \n */
		my_free(blkdata);
		return 1;	/* found 1 newline */
	}

	/* alloc mem */
	mr = (struct mime_record *) my_malloc(sizeof(struct mime_record));

	if (!mr) {
		trace(TRACE_ERROR, "mime_readheader(): out of memory\n");
		my_free(blkdata);
		return -2;
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
				endptr++;
				totallines++;
				if (is_end_of_header(endptr))
					break;
			}
			endptr++;
		}

		if (!(*endptr)) {
			/* end of data block reached (??) */
			my_free(mr);
			*blkidx += (endptr - startptr);
			my_free(blkdata);
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
			while ((delimiter[idx] == ':')
			       || (delimiter[idx] == ' '))
				idx++;

			/* &delimiter[idx] is field value, startptr is field name */
			fieldlen =
			    snprintf(mr->field, MIME_FIELD_MAX, "%s",
				     startptr);
			for (vallen = 0, j = 0;
			     delimiter[idx + j] && vallen < MIME_VALUE_MAX;
			     j++, vallen++) {
				if (delimiter[idx + j] == '\n') {
					mr->value[vallen++] = '\r';
					/* dont count newline here: it is already counted */
				}

				mr->value[vallen] = delimiter[idx + j];
			}

			if (vallen < MIME_VALUE_MAX)
				mr->value[vallen] = 0;
			else
				mr->value[MIME_VALUE_MAX - 1] = 0;

			/* snprintf returns -1 if max is readched (libc <= 2.0.6) or the strlen (libc >= 2.1)
			 * check the value. it does not count the \0.
			 */

			if (fieldlen == -1 || fieldlen >= MIME_FIELD_MAX)
				*headersize += MIME_FIELD_MAX;
			else
				*headersize += fieldlen;

			if (vallen == -1 || vallen >= MIME_VALUE_MAX)
				*headersize += MIME_VALUE_MAX;
			else
				*headersize += vallen;

			*headersize += 4;	/* <field>: <value>\r\n --> four more */


/*	  strncpy(mr->field, startptr, MIME_FIELD_MAX);
	  strncpy(mr->value, &delimiter[idx], MIME_VALUE_MAX);
*/
/*	  trace(TRACE_DEBUG,"mime_readheader(): mimepair found: [%s] [%s] \n",mr->field, mr->value); 
*/
			el = list_nodeadd(mimelist, mr, sizeof(*mr));
			if (!el) {
				trace(TRACE_ERROR,
				      "mime_readheader(): cannot add element to list\n");
				my_free(mr);
				my_free(blkdata);
				return -2;
			}

			/* restore blkdata */
			*delimiter = ':';
		} else {
			/* 
			 * ok invalid mime header, what now ? 
			 * just add it with an empty field name EXCEPT
			 * when the previous stored field value ends on a ';'
			 * in this case probably someone forget to place a \t on the next line
			 * then we will try to add it to the previous element
			 */

			new_add = 1;
			if (el) {
				prev_mr =
				    (struct mime_record *) (el->data);
				prevlen = strlen(prev_mr->value);

				new_add =
				    (prev_mr->value[prevlen - 1] ==
				     ';') ? 0 : 1;
			}

			if (new_add) {
				/* add a new field with no name */
				strcpy(mr->field, "");
				vallen =
				    snprintf(mr->value, MIME_VALUE_MAX,
					     "%s", startptr);

				if (vallen == -1
				    || vallen >= MIME_VALUE_MAX)
					*headersize += MIME_VALUE_MAX;
				else
					*headersize += vallen;

				*headersize += 4;	/* <field>: <value>\r\n --> four more */

				el = list_nodeadd(mimelist, mr,
						  sizeof(*mr));

				if (!el) {
					trace(TRACE_ERROR,
					      "mime_readheader(): cannot add element to list\n");
					my_free(mr);
					my_free(blkdata);
					return -2;
				}
			} else {
				/* try to add the value to the previous one */
				if (prevlen <
				    MIME_VALUE_MAX - (strlen(startptr) +
						      4)) {
					prev_mr->value[prevlen] = '\n';
					prev_mr->value[prevlen + 1] = '\t';

					strcpy(&prev_mr->
					       value[prevlen + 2],
					       startptr);

					*headersize +=
					    (strlen(startptr) + 2);
				} else {
					trace(TRACE_WARNING,
					      "mime_readheader(): failed adding data (length would exceed "
					      "MIME_VALUE_MAX [currently %d])\n",
					      MIME_VALUE_MAX);
				}
			}
		}

		if (cr_nl_present)
			*(endptr - 1) = '\r';
		*endptr = '\n';	/* restore blkdata */

		*blkidx += (endptr - startptr);
		(*blkidx)++;

		startptr = endptr + 1;	/* advance to next field */

		if (*startptr == '\n'
		    || (*startptr == '\r' && *(startptr + 1) == '\n')) {
			/* end of header: double newline */
			totallines++;
			(*blkidx)++;
			(*headersize) += 2;
			trace(TRACE_DEBUG,
			      "mime_readheader(): found double newline; header size: %d lines\n",
			      totallines);
			my_free(mr);
			my_free(blkdata);
			return totallines;
		}

	}

	/* everything down here should be unreachable */

	my_free(mr);		/* no longer need this */

	trace(TRACE_DEBUG, "mime_readheader(): mimeloop finished\n");
	if (valid_mime_lines < 2) {
		trace(TRACE_ERROR,
		      "mime_readheader(): no valid mime headers found\n");
		my_free(blkdata);
		return -1;
	}

	my_free(blkdata);
	/* success ? */
	trace(TRACE_DEBUG, " *** mime_readheader() done ***\n");
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
