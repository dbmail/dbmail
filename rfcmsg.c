/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * $Id$
 * (c) 2000-2002 IC&S, The Netherlands (http://www.ic-s.nl)
 * function implementations for parsing an RFC822/MIME 
 * compliant mail message
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rfcmsg.h"
#include "list.h"
#include "debug.h"
#include "mime.h"
#include "dbmsgbuf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* 
 * frees all the memory associated with a msg
 */
void db_free_msg(mime_message_t * msg)
{
	struct element *tmp;

	if (!msg)
		return;

	/* free the children msg's */
	tmp = list_getstart(&msg->children);

	while (tmp) {
		db_free_msg((mime_message_t *) tmp->data);
		tmp = tmp->nextnode;
	}

	tmp = list_getstart(&msg->children);
	list_freelist(&tmp);

	tmp = list_getstart(&msg->mimeheader);
	list_freelist(&tmp);

	tmp = list_getstart(&msg->rfcheader);
	list_freelist(&tmp);

	memset(msg, 0, sizeof(*msg));
}


/* 
 * reverses the children lists of a msg
 */
void db_reverse_msg(mime_message_t * msg)
{
	struct element *tmp;

	if (!msg)
		return;

	/* reverse the children msg's */
	tmp = list_getstart(&msg->children);

	while (tmp) {
		db_reverse_msg((mime_message_t *) tmp->data);
		tmp = tmp->nextnode;
	}

	/* reverse this list */
	msg->children.start = dbmail_list_reverse(msg->children.start);

	/* reverse header items */
	msg->mimeheader.start = dbmail_list_reverse(msg->mimeheader.start);
	msg->rfcheader.start = dbmail_list_reverse(msg->rfcheader.start);
}

/*
 * db_fetch_headers()
 *
 * builds up an array containing message headers and the start/end position of the 
 * associated body part(s)
 *
 * creates a linked-list of headers found
 *
 * NOTE: there are no checks performed to verify that the indicated msg isn't expunged 
 *       (status STATUS_DELETE) or has been inserted completely. This should be done before calling
 *       this function (unless, of course, it is your intention to specifically parse an 
 *       incomplete message or an expunged one).
 *
 * returns:
 * -3 memory error
 * -2 dbase error
 * -1 parse error but msg is retrieved as plaintext
 *  0 success
 */
int db_fetch_headers(u64_t msguid, mime_message_t * msg)
{
	int result, level = 0, maxlevel = -1;

	if (db_init_msgfetch(msguid) != 1) {
		trace(TRACE_ERROR,
		      "db_fetch_headers(): could not init msgfetch\n");
		return -2;
	}

	result = db_start_msg(msg, NULL, &level, maxlevel);	/* fetch message */
	if (result < 0) {
		trace(TRACE_INFO,
		      "db_fetch_headers(): error fetching message, ID: %llu\n",
		      msguid);
		trace(TRACE_INFO,
		      "db_fetch_headers(): got error at level %d\n",
		      level);

		db_close_msgfetch();
		db_free_msg(msg);

		if (result < -1)
			return result;	/* memory/dbase error */

		/* 
		 * so an error occurred parsing the message. 
		 * try to lower the maxlevel of recursion
		 */

		for (maxlevel = level - 1; maxlevel >= 0; maxlevel--) {
			trace(TRACE_DEBUG,
			      "db_fetch_headers(): trying to fetch at maxlevel %d...\n",
			      maxlevel);

			if (db_init_msgfetch(msguid) != 1) {
				trace(TRACE_ERROR,
				      "db_fetch_headers(): could not init msgfetch\n");
				return -2;
			}

			level = 0;
			result = db_start_msg(msg, NULL, &level, maxlevel);

			db_close_msgfetch();

			if (result != -1)
				break;

			db_free_msg(msg);
		}

		if (result < -1) {
			db_free_msg(msg);
			return result;
		}

		if (result >= 0) {
			trace(TRACE_WARNING,
			      "db_fetch_headers(): succesfully recovered erroneous message %llu\n",
			      msguid);
			db_reverse_msg(msg);
			return 0;
		}


		/* ok still problems... try to make a message */
		if (db_init_msgfetch(msguid) != 1) {
			trace(TRACE_ERROR,
			      "db_fetch_headers(): could not init msgfetch\n");
			return -2;
		}

		result = db_parse_as_text(msg);
		if (result < 0) {
			/* probably some serious dbase error */
			trace(TRACE_ERROR,
			      "db_fetch_headers(): could not recover message as plain text\n");
			db_free_msg(msg);
			return result;
		}

		trace(TRACE_WARNING,
		      "db_fetch_headers(): message recovered as plain text\n");
		db_close_msgfetch();
		return -1;
	}

	db_reverse_msg(msg);

	db_close_msgfetch();
	return 0;
}


/*
 * db_start_msg()
 *
 * parses a msg; uses msgbuf_buf[] as data
 *
 * level & maxlevel are used to determine the max level of recursion (error-recovery)
 * level is raised before calling add_mime_children() except when maxlevel and level
 * are both zero, in that case the message is split in header/rest, add_mime_children
 * will not be called at all.
 *
 * returns the number of lines parsed or -1 on parse error, -2 on dbase error, -3 on memory error
 */
int db_start_msg(mime_message_t * msg, char *stopbound, int *level,
		 int maxlevel)
{
	int len, sblen, result, totallines = 0, nlines, hdrlines;
	struct mime_record *mr;
	char *newbound, *bptr;
	int continue_recursion = (maxlevel == 0 && *level == 0) ? 0 : 1;

	trace(TRACE_DEBUG, "db_start_msg(): starting, stopbound: '%s'\n",
	      stopbound ? stopbound : "<null>");

	list_init(&msg->children);
	msg->message_has_errors = (!continue_recursion);


	/* read header */
	if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1)
		return -2;

	if ((hdrlines =
	     mime_readheader(&msgbuf_buf[msgbuf_idx], &msgbuf_idx,
			     &msg->rfcheader, &msg->rfcheadersize)) < 0)
		return hdrlines;	/* error reading header */

	db_give_msgpos(&msg->bodystart);
	msg->rfcheaderlines = hdrlines;

	mime_findfield("content-type", &msg->rfcheader, &mr);
	if (continue_recursion &&
	    mr
	    && strncasecmp(mr->value, "multipart",
			   strlen("multipart")) == 0) {
		trace(TRACE_DEBUG,
		      "db_start_msg(): found multipart msg\n");

		/* multipart msg, find new boundary */
		for (bptr = mr->value; *bptr; bptr++)
			if (strncasecmp
			    (bptr, "boundary=",
			     sizeof("boundary=") - 1) == 0)
				break;

		if (!bptr) {
			trace(TRACE_WARNING,
			      "db_start_msg(): could not find a new msg-boundary\n");
			return -1;	/* no new boundary ??? */
		}

		bptr += sizeof("boundary=") - 1;
		if (*bptr == '\"') {
			bptr++;
			newbound = bptr;
			while (*newbound && *newbound != '\"')
				newbound++;
		} else {
			newbound = bptr;
			while (*newbound && !isspace(*newbound)
			       && *newbound != ';')
				newbound++;
		}

		len = newbound - bptr;
		if (!(newbound = (char *) my_malloc(len + 1))) {
			trace(TRACE_ERROR,
			      "db_start_msg(): out of memory\n");
			return -3;
		}

		strncpy(newbound, bptr, len);
		newbound[len] = '\0';

		trace(TRACE_DEBUG,
		      "db_start_msg(): found new boundary: [%s], msgbuf_idx %llu\n",
		      newbound, msgbuf_idx);

		/* advance to first boundary */
		if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1) {
			trace(TRACE_ERROR,
			      "db_startmsg(): error updating msgbuf\n");
			my_free(newbound);
			return -2;
		}

		while (msgbuf_buf[msgbuf_idx]) {
			if (strncmp
			    (&msgbuf_buf[msgbuf_idx], newbound,
			     strlen(newbound)) == 0)
				break;

			if (msgbuf_buf[msgbuf_idx] == '\n')
				totallines++;

			msgbuf_idx++;
		}

		if (!msgbuf_buf[msgbuf_idx]) {
			trace(TRACE_WARNING,
			      "db_start_msg(): unexpected end-of-data\n");
			my_free(newbound);
			return -1;
		}

		msgbuf_idx += strlen(newbound);	/* skip the boundary */
		msgbuf_idx++;	/* skip \n */
		totallines++;	/* and count it */

		/* find MIME-parts */
		(*level)++;
		if ((nlines =
		     db_add_mime_children(&msg->children, newbound, level,
					  maxlevel)) < 0) {
			trace(TRACE_WARNING,
			      "db_start_msg(): error adding MIME-children\n");
			my_free(newbound);
			return nlines;
		}
		(*level)--;
		totallines += nlines;

		/* skip stopbound if present */
		if (stopbound) {
			sblen = strlen(stopbound);
			msgbuf_idx += (2 + sblen);	/* double hyphen preceeds */
		}

		my_free(newbound);
		newbound = NULL;

		if (msgbuf_idx > 0) {
			/* walk back because bodyend is inclusive */
			msgbuf_idx--;
			db_give_msgpos(&msg->bodyend);
			msgbuf_idx++;
		} else
			db_give_msgpos(&msg->bodyend);	/* this case should never happen... */


		msg->bodysize =
		    db_give_range_size(&msg->bodystart, &msg->bodyend);
		msg->bodylines = totallines;

		return totallines + hdrlines;	/* done */
	} else {
		/* single part msg, read untill stopbound OR end of buffer */
		trace(TRACE_DEBUG,
		      "db_start_msg(): found singlepart msg\n");

		if (stopbound) {
			sblen = strlen(stopbound);

			while (msgbuf_buf[msgbuf_idx]) {
				if (db_update_msgbuf(sblen + 3) == -1)
					return -2;

				if (msgbuf_buf[msgbuf_idx] == '\n')
					msg->bodylines++;

				if (msgbuf_buf[msgbuf_idx + 1] == '-'
				    && msgbuf_buf[msgbuf_idx + 2] == '-'
				    && strncmp(&msgbuf_buf[msgbuf_idx + 3],
					       stopbound, sblen) == 0) {
					db_give_msgpos(&msg->bodyend);
					msg->bodysize =
					    db_give_range_size(&msg->
							       bodystart,
							       &msg->
							       bodyend);

					msgbuf_idx++;	/* msgbuf_buf[msgbuf_idx] == '-' now */

					/* advance to after stopbound */
					msgbuf_idx += sblen + 2;	/* (add 2 cause double hyphen preceeds) */
					while (isspace
					       (msgbuf_buf[msgbuf_idx])) {
						if (msgbuf_buf[msgbuf_idx]
						    == '\n')
							totallines++;
						msgbuf_idx++;
					}

					trace(TRACE_DEBUG,
					      "db_start_msg(): stopbound reached\n");
					return (totallines +
						msg->bodylines + hdrlines);
				}

				msgbuf_idx++;
			}

			/* end of buffer reached, invalid message encountered: there should be a stopbound! */
			/* but lets pretend there's nothing wrong... */
			db_give_msgpos(&msg->bodyend);
			msg->bodysize =
			    db_give_range_size(&msg->bodystart,
					       &msg->bodyend);
			totallines += msg->bodylines;

			trace(TRACE_WARNING,
			      "db_start_msg(): no stopbound where expected...\n");

/*	  return -1;
*/
		} else {
			/* walk on till end of buffer */
			result = 1;
			while (1) {
				for (;
				     msgbuf_idx < msgbuf_buflen - 1
				     && msgbuf_buf[msgbuf_idx];
				     msgbuf_idx++)
					if (msgbuf_buf[msgbuf_idx] == '\n')
						msg->bodylines++;

				if (result == 0) {
					/* end of msg reached, one char left in msgbuf */
					if (msgbuf_buf[msgbuf_idx] == '\n')
						msg->bodylines++;

					break;
				}

				result =
				    db_update_msgbuf(MSGBUF_FORCE_UPDATE);
				if (result == -1)
					return -2;
			}

			db_give_msgpos(&msg->bodyend);
			msg->bodysize =
			    db_give_range_size(&msg->bodystart,
					       &msg->bodyend);
			totallines += msg->bodylines;
		}
	}

	trace(TRACE_DEBUG, "db_start_msg(): exit\n");

	return totallines;
}



/*
 * assume to enter just after a splitbound 
 * returns -1 on parse error, -2 on dbase error, -3 on memory error
 */
int db_add_mime_children(struct list *brothers, char *splitbound,
			 int *level, int maxlevel)
{
	mime_message_t part;
	struct mime_record *mr;
	int sblen, nlines, totallines = 0, len;
	u64_t dummy;
	char *bptr, *newbound;
	int continue_recursion = (maxlevel < 0
				  || *level < maxlevel) ? 1 : 0;

	trace(TRACE_DEBUG,
	      "db_add_mime_children(): starting, splitbound: '%s'\n",
	      splitbound);
	sblen = strlen(splitbound);

	do {
		db_update_msgbuf(MSGBUF_FORCE_UPDATE);
		memset(&part, 0, sizeof(part));
		part.message_has_errors = (!continue_recursion);

		/* should have a MIME header right here */
		if ((nlines =
		     mime_readheader(&msgbuf_buf[msgbuf_idx], &msgbuf_idx,
				     &part.mimeheader, &dummy)) < 0) {
			trace(TRACE_WARNING,
			      "db_add_mime_children(): error reading MIME-header\n");
			db_free_msg(&part);
			return nlines;	/* error reading header */
		}
		totallines += nlines;

		mime_findfield("content-type", &part.mimeheader, &mr);

		if (continue_recursion &&
		    mr
		    && strncasecmp(mr->value, "message/rfc822",
				   strlen("message/rfc822")) == 0) {
			trace(TRACE_DEBUG,
			      "db_add_mime_children(): found an RFC822 message\n");

			/* a message will follow */
			if ((nlines =
			     db_start_msg(&part, splitbound, level,
					  maxlevel)) < 0) {
				trace(TRACE_WARNING,
				      "db_add_mime_children(): error retrieving message\n");
				db_free_msg(&part);
				return nlines;
			}
			trace(TRACE_DEBUG,
			      "db_add_mime_children(): got %d newlines from start_msg()\n",
			      nlines);
			totallines += nlines;
			part.mimerfclines = nlines;
		} else if (continue_recursion &&
			   mr
			   && strncasecmp(mr->value, "multipart",
					  strlen("multipart")) == 0) {
			trace(TRACE_DEBUG,
			      "db_add_mime_children(): found a MIME multipart sub message\n");

			/* multipart msg, find new boundary */
			for (bptr = mr->value; *bptr; bptr++)
				if (strncasecmp
				    (bptr, "boundary=",
				     sizeof("boundary=") - 1) == 0)
					break;

			if (!bptr) {
				trace(TRACE_WARNING,
				      "db_add_mime_children(): could not find a new msg-boundary\n");
				db_free_msg(&part);
				return -1;	/* no new boundary ??? */
			}

			bptr += sizeof("boundary=") - 1;
			if (*bptr == '\"') {
				bptr++;
				newbound = bptr;
				while (*newbound && *newbound != '\"')
					newbound++;
			} else {
				newbound = bptr;
				while (*newbound && !isspace(*newbound)
				       && *newbound != ';')
					newbound++;
			}

			len = newbound - bptr;
			if (!(newbound = (char *) my_malloc(len + 1))) {
				trace(TRACE_ERROR,
				      "db_add_mime_children(): out of memory\n");
				db_free_msg(&part);
				return -3;
			}

			strncpy(newbound, bptr, len);
			newbound[len] = '\0';

			trace(TRACE_DEBUG,
			      "db_add_mime_children(): found new boundary: [%s], msgbuf_idx %llu\n",
			      newbound, msgbuf_idx);


			/* advance to first boundary */
			if (db_update_msgbuf(MSGBUF_FORCE_UPDATE) == -1) {
				trace(TRACE_ERROR,
				      "db_add_mime_children(): error updating msgbuf\n");
				db_free_msg(&part);
				my_free(newbound);
				return -2;
			}

			while (msgbuf_buf[msgbuf_idx]) {
				if (strncmp
				    (&msgbuf_buf[msgbuf_idx], newbound,
				     strlen(newbound)) == 0)
					break;

				if (msgbuf_buf[msgbuf_idx] == '\n') {
					totallines++;
					part.bodylines++;
				}

				msgbuf_idx++;
			}

			if (!msgbuf_buf[msgbuf_idx]) {
				trace(TRACE_WARNING,
				      "db_add_mime_children(): unexpected end-of-data\n");
				my_free(newbound);
				db_free_msg(&part);
				return -1;
			}

			msgbuf_idx += strlen(newbound);	/* skip the boundary */
			msgbuf_idx++;	/* skip \n */
			totallines++;	/* and count it */
			part.bodylines++;
			db_give_msgpos(&part.bodystart);	/* remember position */

			(*level)++;
			if ((nlines =
			     db_add_mime_children(&part.children, newbound,
						  level, maxlevel)) < 0) {
				trace(TRACE_WARNING,
				      "db_add_mime_children(): error adding mime children\n");
				my_free(newbound);
				db_free_msg(&part);
				return nlines;
			}
			(*level)--;

			my_free(newbound);
			newbound = NULL;
			msgbuf_idx += sblen + 2;	/* skip splitbound */

			if (msgbuf_idx > 0) {
				/* walk back because bodyend is inclusive */
				msgbuf_idx--;
				db_give_msgpos(&part.bodyend);
				msgbuf_idx++;
			} else
				db_give_msgpos(&part.bodyend);	/* this case should never happen... */


			part.bodysize =
			    db_give_range_size(&part.bodystart,
					       &part.bodyend);
			part.bodylines += nlines;
			totallines += nlines;
		} else {
			trace(TRACE_DEBUG,
			      "db_add_mime_children(): expecting body data...\n");

			/* just body data follows, advance to splitbound */
			db_give_msgpos(&part.bodystart);

			while (msgbuf_buf[msgbuf_idx]) {
				if (db_update_msgbuf(sblen + 3) == -1) {
					db_free_msg(&part);
					return -2;
				}

				if (msgbuf_buf[msgbuf_idx] == '\n')
					part.bodylines++;

				if (msgbuf_buf[msgbuf_idx + 1] == '-'
				    && msgbuf_buf[msgbuf_idx + 2] == '-'
				    && strncmp(&msgbuf_buf[msgbuf_idx + 3],
					       splitbound, sblen) == 0)
					break;

				msgbuf_idx++;
			}

			/* at this point msgbuf_buf[msgbuf_idx] is either
			 * 0 (end of data) -- invalid message!
			 * or the character right before '--<splitbound>'
			 */

			totallines += part.bodylines;

			if (!msgbuf_buf[msgbuf_idx]) {
				trace(TRACE_WARNING,
				      "db_add_mime_children(): unexpected end of data\n");
				db_free_msg(&part);
				return -1;	/* ?? splitbound should follow */
			}

			db_give_msgpos(&part.bodyend);
			part.bodysize =
			    db_give_range_size(&part.bodystart,
					       &part.bodyend);

			msgbuf_idx++;	/* msgbuf_buf[msgbuf_idx] == '-' after this statement */

			msgbuf_idx += sblen + 2;	/* skip the boundary & double hypen */
		}

		/* add this part to brother list */
		if (list_nodeadd(brothers, &part, sizeof(part)) == NULL) {
			trace(TRACE_WARNING,
			      "db_add_mime_children(): could not add node\n");
			db_free_msg(&part);
			return -3;
		}

		/* if double hyphen ('--') follows we're done */
		if (msgbuf_buf[msgbuf_idx] == '-'
		    && msgbuf_buf[msgbuf_idx + 1] == '-') {
			trace(TRACE_DEBUG,
			      "db_add_mime_children(): found end after boundary [%s],\n",
			      splitbound);
			trace(TRACE_DEBUG,
			      "                        followed by [%.*s],\n",
			      48, &msgbuf_buf[msgbuf_idx]);

			msgbuf_idx += 2;	/* skip hyphens */

			/* probably some newlines will follow (not specified but often there) */
			while (msgbuf_buf[msgbuf_idx] == '\n') {
				totallines++;
				msgbuf_idx++;
			}

			return totallines;
		}

		if (msgbuf_buf[msgbuf_idx] == '\n') {
			totallines++;
			msgbuf_idx++;	/* skip the newline itself */
		}
	}
	while (msgbuf_buf[msgbuf_idx]);

	trace(TRACE_WARNING,
	      "db_add_mime_children(): sudden end of message\n");
	return totallines;

/*  trace(TRACE_ERROR,"db_add_mime_children(): invalid message (no ending boundary found)\n");
  return -1;
*/
}


/*
 * db_parse_as_text()
 * 
 * parses a message as a block of plain text; an explaining header is created
 * note that this will disturb the length calculations...
 * this function is called when normal parsing fails.
 * 
 * returns -1 on dbase failure, -2 on memory error
 */
int db_parse_as_text(mime_message_t * msg)
{
	int result;
	struct mime_record mr;
	struct element *el = NULL;

	memset(msg, 0, sizeof(*msg));

	strcpy(mr.field, "subject");
	strcpy(mr.value,
	       "dbmail IMAP server info: this message could not be parsed");
	el = list_nodeadd(&msg->rfcheader, &mr, sizeof(mr));
	if (!el)
		return -3;

	strcpy(mr.field, "from");
	strcpy(mr.value, "imapserver@dbmail.org");
	el = list_nodeadd(&msg->rfcheader, &mr, sizeof(mr));
	if (!el)
		return -3;

	msg->rfcheadersize =
	    strlen
	    ("subject: dbmail IMAP server info: this message could not be parsed\r\n")
	    + strlen("from: imapserver@dbmail.org\r\n");
	msg->rfcheaderlines = 4;

	db_give_msgpos(&msg->bodystart);

	/* walk on till end of buffer */
	result = 1;
	while (1) {
		for (; msgbuf_idx < msgbuf_buflen - 1; msgbuf_idx++)
			if (msgbuf_buf[msgbuf_idx] == '\n')
				msg->bodylines++;

		if (result == 0) {
			/* end of msg reached, one char left in msgbuf_buf */
			if (msgbuf_buf[msgbuf_idx] == '\n')
				msg->bodylines++;

			break;
		}

		result = db_update_msgbuf(MSGBUF_FORCE_UPDATE);
		if (result == -1)
			return -2;
	}

	db_give_msgpos(&msg->bodyend);
	msg->bodysize = db_give_range_size(&msg->bodystart, &msg->bodyend);

	return 0;
}


/*
 * db_msgdump()
 *
 * dumps a message to stderr
 * returns the size (in bytes) that the message occupies in memory
 */
int db_msgdump(mime_message_t * msg, u64_t msguid, int level)
{
	struct element *curr;
	struct mime_record *mr;
	char *spaces;
	int size = sizeof(mime_message_t);

	if (level < 0)
		return 0;

	if (!msg) {
		trace(TRACE_DEBUG, "db_msgdump: got null\n");
		return 0;
	}

	spaces = (char *) my_malloc(3 * level + 1);
	if (!spaces)
		return 0;

	memset(spaces, ' ', 3 * level);
	spaces[3 * level] = 0;


	trace(TRACE_DEBUG, "%sMIME-header: \n", spaces);
	curr = list_getstart(&msg->mimeheader);
	if (!curr)
		trace(TRACE_DEBUG, "%s%snull\n", spaces, spaces);
	else {
		while (curr) {
			mr = (struct mime_record *) curr->data;
			trace(TRACE_DEBUG, "%s%s[%s] : [%s]\n", spaces,
			      spaces, mr->field, mr->value);
			curr = curr->nextnode;
			size += sizeof(struct mime_record);
		}
	}
	trace(TRACE_DEBUG, "%s*** MIME-header end\n", spaces);

	trace(TRACE_DEBUG, "%sRFC822-header: \n", spaces);
	curr = list_getstart(&msg->rfcheader);
	if (!curr)
		trace(TRACE_DEBUG, "%s%snull\n", spaces, spaces);
	else {
		while (curr) {
			mr = (struct mime_record *) curr->data;
			trace(TRACE_DEBUG, "%s%s[%s] : [%s]\n", spaces,
			      spaces, mr->field, mr->value);
			curr = curr->nextnode;
			size += sizeof(struct mime_record);
		}
	}
	trace(TRACE_DEBUG, "%s*** RFC822-header end\n", spaces);

	trace(TRACE_DEBUG, "%s*** Body range:\n", spaces);
	trace(TRACE_DEBUG,
	      "%s%s(%llu, %llu) - (%llu, %llu), size: %llu, newlines: %llu\n",
	      spaces, spaces, msg->bodystart.block, msg->bodystart.pos,
	      msg->bodyend.block, msg->bodyend.pos, msg->bodysize,
	      msg->bodylines);


/*  trace(TRACE_DEBUG,"body: \n");
  db_dump_range(msg->bodystart, msg->bodyend, msguid);
  trace(TRACE_DEBUG,"*** body end\n");
*/
	trace(TRACE_DEBUG, "%sChildren of this msg:\n", spaces);

	curr = list_getstart(&msg->children);
	while (curr) {
		size +=
		    db_msgdump((mime_message_t *) curr->data, msguid,
			       level + 1);
		curr = curr->nextnode;
	}
	trace(TRACE_DEBUG, "%s*** child list end\n", spaces);

	my_free(spaces);
	return size;
}
