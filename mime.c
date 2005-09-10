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

/* return 1 if this is the end of a header. That is, it returns 1 if
 * the next character is a non-whitespace character, or a newline or
 * carriage return + newline. If the next character is a white space
 * character, but not a newline, or carriage return + newline, the
 * header continues.
 */
static int is_end_of_header(const char *s)
{
	if (!isspace(s[1]))
		return 1;

	if (s[1] == '\n')
		return 1;

	if (s[1] == '\r' && s[2] == '\n')
		return 1;

	return 0;
}

static void _register_header(const char *field, const char *value, gpointer mimelist)
{
	struct mime_record *mr = g_new0(struct mime_record, 1);
	if (! mr)
		trace(TRACE_FATAL,"%s,%s: oom", __FILE__, __func__);

	g_strlcpy(mr->field, field, MIME_FIELD_MAX);
	g_strlcpy(mr->value, value, MIME_VALUE_MAX);
	dm_list_nodeadd((struct dm_list *)mimelist, mr, sizeof(*mr));
	g_free(mr);
}

/* mime_fetch_headers()
 *
 * copy the header names and values to a dm_list of mime_records.
 */

int mime_fetch_headers(const char *datablock, struct dm_list *mimelist) 
{
	GMimeMessage *message;
	GString *raw = g_string_new(datablock);
	struct DbmailMessage *m = dbmail_message_new();
	if (! m)
		trace(TRACE_FATAL,"%s,%s: oom", __FILE__, __func__);
	
	m = dbmail_message_init_with_string(m, raw);
	g_mime_header_foreach(m->content->headers, _register_header, (gpointer)mimelist);
	g_string_free(raw,TRUE);
	
	/* dbmail expects the mime-headers of the message's mimepart as part of the rfcheaders */
	if (dbmail_message_get_class(m) == DBMAIL_MESSAGE && GMIME_MESSAGE(m->content)->mime_part) {
		message = (GMimeMessage *)(m->content);
		g_mime_header_foreach(GMIME_OBJECT(message->mime_part)->headers, _register_header, (gpointer)mimelist);
	}

	dbmail_message_free(m);
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
 * returns -1 on parse failure, -2 on memory error; 0 on succes
 */


int mime_readheader(const char *datablock, u64_t * msgbuf_idx, struct dm_list *mimelist, u64_t * headersize)
{
	char *raw, *crlf;
	struct DbmailMessage *message;
	GString *mimepart = g_string_new(datablock);
	
	/* moved header-parsing to separate function */
	mime_fetch_headers(datablock, mimelist);

	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message,mimepart);
	g_string_free(mimepart,TRUE);
	
	crlf = dbmail_message_hdrs_to_string(message,TRUE);
	raw = dbmail_message_hdrs_to_string(message,FALSE);
	
	*headersize = strlen(crlf);
	*msgbuf_idx = strlen(raw);

	g_free(crlf);
	g_free(raw);

	return (*headersize - *msgbuf_idx);
}

/*
 * mime_findfield()
 *
 * finds a MIME header field
 *
 */
void mime_findfield(const char *fname, struct dm_list *mimelist,
		    struct mime_record **mr)
{
	struct element *current;

	trace(TRACE_DEBUG, "%s,%s: scanning for %s",
			__FILE__, __func__, fname);
	
	current = dm_list_getstart(mimelist);
	while (current) {
		*mr = current->data;	/* get field/value */
		if (strcasecmp((*mr)->field, fname) == 0)
			return;	/* found */
		current = current->nextnode;
	}
	*mr = NULL;
}


int mail_address_build_list(char *scan_for_field, struct dm_list *targetlist,
		  struct dm_list *mimelist)
{
	struct mime_record *mr;
	InternetAddressList *ialisthead, *ialist;
	InternetAddress *ia;

	if (!scan_for_field || !targetlist || !mimelist) {
		trace(TRACE_ERROR, "%s,%s: received a NULL argument\n",
				__FILE__, __func__);
		return -1;
	}

	trace(TRACE_INFO, "%s,%s: mail address parser starting",
			__FILE__, __func__);

	mime_findfield(scan_for_field, mimelist, &mr);
	if (mr == NULL)
		return 0;
	
	if ((ialist = internet_address_parse_string(mr->value)) == NULL)
		return -1;

	ialisthead = ialist;
	while (1) {
		ia = ialist->address;
		dm_list_nodeadd(targetlist,ia->value.addr, strlen(ia->value.addr) + 1);
		if (! ialist->next)
			break;
		ialist = ialist->next;
	}
	
	internet_address_list_destroy(ialisthead);

	trace(TRACE_DEBUG, "%s,%s: found %ld emailaddresses",
			__FILE__, __func__,
			targetlist->total_nodes);

	if (targetlist->total_nodes == 0)	/* no addresses found */
		return -1;

	return 0;
}

