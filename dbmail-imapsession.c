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

/* $Id: dbmail-imapsession.c 1829 2005-08-01 14:53:53Z paul $
 * 
 * imaputil.c
 *
 * IMAP-server utility functions implementations
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

#include "dbmail.h"
#include "imaputil.h"
#include "imap4.h"
#include "debug.h"
#include "db.h"
#include "memblock.h"
#include "dbsearch.h"
#include "dbmsgbuf.h"
#include "rfcmsg.h"
#include "dbmail-imapsession.h"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE (10*1024)
#endif

#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];


/* cache */
extern cache_t cached_msg;

extern const char *month_desc[];
extern char base64encodestring[];
/* returned by date_sql2imap() */
#define IMAP_STANDARD_DATE "03-Nov-1979 00:00:00 +0000"
extern char _imapdate[IMAP_INTERNALDATE_LEN];

/* returned by date_imap2sql() */
#define SQL_STANDARD_DATE "1979-11-03 00:00:00"
extern char _sqldate[SQL_INTERNALDATE_LEN + 1];
extern const int month_len[];
extern const char *item_desc[];
extern const char *envelope_items[];
extern const char *imap_flag_desc[];
extern const char *imap_flag_desc_escaped[];

static int _imap_session_fetch_parse_partspec(struct ImapSession *self, int idx);
static int _imap_session_fetch_parse_octet_range(struct ImapSession *self, int idx);

static GList * _imap_get_structure(mime_message_t * msg, int show_extension_data);
static GList * _imap_get_addresses(struct mime_record *mr);
static GList * _imap_get_envelope(struct dm_list *rfcheader);
static GList * _imap_get_mime_parameters(struct mime_record *mr, int force_subtype, int only_extension);

static void _imap_show_body_sections(struct ImapSession *self);
static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data);
	

static u64_t get_dumpsize(body_fetch_t *bodyfetch, u64_t tmpdumpsize); 
/* 
 *
 * initializer and accessors for ImapSession
 *
 */

struct ImapSession * dbmail_imap_session_new(void)
{
	struct ImapSession * self;
	self = g_new0(struct ImapSession,1);
	if (! self)
		trace(TRACE_ERROR,"%s,%s: OOM error", __FILE__, __func__);

	self->ci = g_new0(clientinfo_t,1);
	self->fi = g_new0(fetch_items_t,1);
	self->msginfo = g_new0(msginfo_t,1);
	
	return self;
}

struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self)
{
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)g_free,NULL);
	memset(self->fi,'\0',sizeof(fetch_items_t));
	return self;
}
     
struct ImapSession * dbmail_imap_session_setClientinfo(struct ImapSession * self, clientinfo_t *ci)
{
	memcpy(self->ci, ci, sizeof(clientinfo_t));
	return self;
}
struct ImapSession * dbmail_imap_session_setTag(struct ImapSession * self, char * tag)
{
	self->tag = tag;
	return self;
}
struct ImapSession * dbmail_imap_session_setCommand(struct ImapSession * self, char * command)
{
	self->command = command;
	return self;
}
struct ImapSession * dbmail_imap_session_setArgs(struct ImapSession * self, char ** args)
{
	self->args = args;
	return self;
}
struct ImapSession * dbmail_imap_session_setFi(struct ImapSession * self, fetch_items_t *fi)
{
	self->fi = fi;
	return self;
}
struct ImapSession * dbmail_imap_session_setMsginfo(struct ImapSession * self, msginfo_t * msginfo)
{
	self->msginfo = msginfo;
	return self;
}
void dbmail_imap_session_delete(struct ImapSession * self)
{
	close_cache();
	if (self->ci->userData) {
		null_free(((imap_userdata_t*)self->ci->userData)->mailbox.seq_list);
		null_free(self->ci->userData);
	}
	dbmail_imap_session_bodyfetch_free(self);
	
	if (self->fi)
		g_free(self->fi);
	if (self->ci)
		g_free(self->ci);
	if (self->msginfo)
		g_free(self->msginfo);
	
	g_free(self);
}


/*************************************************************************************
 *
 *
 * imap utilities using ImapSession
 *
 *
 ************************************************************************************/


/* 
 * _imap_get_structure()
 *
 * retrieves the MIME-IMB structure of a message. The msg should be in the format
 * as build by db_fetch_headers().
 *
 * shows extension data if show_extension_data != 0
 *
 * returns GList on success, NULL on error
 */

GList * _imap_get_structure(mime_message_t * msg, int show_extension_data)
{
	struct mime_record *mr;
	struct element *curr;
	struct dm_list *header_to_use;
	mime_message_t rfcmsg;
	char *subtype, *extension, *newline;
	int is_mime_multipart = 0, is_rfc_multipart = 0;
	int rfc822 = 0;
	
	GList *tlist = NULL, *list = NULL;
	GString *tmp = g_string_new("");
	gchar *pstring;
	
	trace(TRACE_DEBUG,"%s,%s", __FILE__,__func__);
	
	mime_findfield("content-type", &msg->mimeheader, &mr);
	is_mime_multipart = (mr
			     && strncasecmp(mr->value, "multipart", strlen("multipart")) == 0
			     && !msg->message_has_errors);

	mime_findfield("content-type", &msg->rfcheader, &mr);
	is_rfc_multipart = (mr
			    && strncasecmp(mr->value, "multipart", strlen("multipart")) == 0
			    && !msg->message_has_errors);

	/* eddy */
	rfc822 = (mr && strncasecmp(mr->value, "message/rfc822", strlen("message/rfc822")) == 0); 

	if (rfc822 || (!is_rfc_multipart && !is_mime_multipart)) {
		/* show basic fields:
		 * content-type, content-subtype, (parameter list), 
		 * content-id, content-description, content-transfer-encoding,
		 * size
		 */

		if (msg->mimeheader.start == NULL)
			header_to_use = &msg->rfcheader;	/* we're dealing with a single-part RFC msg here */
		else
			header_to_use = &msg->mimeheader;	/* we're dealing with a pure-MIME header here */

		mime_findfield("content-type", header_to_use, &mr);
		if (mr && strlen(mr->value) > 0) {
			
			tlist = _imap_get_mime_parameters(mr, 1, 0);
			
			tmp = g_list_join(tlist," ");
			
			dbmail_imap_plist_free(tlist);
			tlist = NULL;
			
			list = g_list_append(list, g_strdup(tmp->str));
		} else
			list = g_list_append(list, g_strdup("\"TEXT\" \"PLAIN\" (\"CHARSET\" \"US-ASCII\")"));	/* default */

		mime_findfield("content-id", header_to_use, &mr);
		if (mr && strlen(mr->value) > 0) { 
			list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
		} else
			list = g_list_append(list, g_strdup("NIL"));

		mime_findfield("content-description", header_to_use, &mr);
		if (mr && strlen(mr->value) > 0) {
			list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
		} else
			list = g_list_append(list, g_strdup("NIL"));

		mime_findfield("content-transfer-encoding", header_to_use, &mr);
		if (mr && strlen(mr->value) > 0) {
			list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
		} else
			list = g_list_append(list, g_strdup("\"7BIT\""));

		/* now output size */
		/* add msg->bodylines because \n is dumped as \r\n */
		if (msg->mimeheader.start && msg->rfcheader.start)
			list = g_list_append_printf(list, "%llu",
				msg->bodysize + msg->mimerfclines +
				msg->rfcheadersize - msg->rfcheaderlines);
		else
			list = g_list_append_printf(list, "%llu", msg->bodysize + msg->bodylines);


		/* now check special cases, first case: message/rfc822 */
		mime_findfield("content-type", header_to_use, &mr);
		if (mr && strncasecmp(mr->value, "message/rfc822", strlen("message/rfc822")) == 0
		    && header_to_use != &msg->rfcheader) {
			/* msg/rfc822 found; extra items to be displayed:
			 * (a) body envelope of rfc822 msg
			 * (b) body structure of rfc822 msg
			 * (c) msg size (lines)
			 */

			tlist = _imap_get_envelope(&msg->rfcheader);
			
			list = g_list_append(list, dbmail_imap_plist_as_string(tlist));

			dbmail_imap_plist_free(tlist);
			tlist = NULL;

			memmove(&rfcmsg, msg, sizeof(rfcmsg));
			rfcmsg.mimeheader.start = NULL;	/* forget MIME-part */

			/* start recursion */
			tlist = _imap_get_structure(&rfcmsg, show_extension_data);
			
			list = g_list_append(list, dbmail_imap_plist_as_string(tlist));
			
			dbmail_imap_plist_free(tlist);
			tlist = NULL;
			
			/* output # of lines */
			list = g_list_append_printf(list, "%llu", msg->bodylines);
		}
		/* now check second special case: text 
		 * NOTE: if 'content-type' is absent, TEXT is assumed 
		 */
		if ((mr && strncasecmp(mr->value, "text", strlen("text")) == 0) || !mr) {
			/* output # of lines */
			if (msg->mimeheader.start && msg->rfcheader.start)
				list = g_list_append_printf(list, "%llu", msg->mimerfclines);
			else
				list = g_list_append_printf(list, "%llu", msg->bodylines);
		}

		if (show_extension_data) {
			mime_findfield("content-md5", header_to_use, &mr);
			if (mr && strlen(mr->value) > 0) {
				list = g_list_append_printf(list, dbmail_imap_astring_as_string(mr->value));
			} else
				list = g_list_append(list, g_strdup("NIL"));

			mime_findfield("content-disposition", header_to_use, &mr);
			if (mr && strlen(mr->value) > 0) {
				tlist = _imap_get_mime_parameters(mr, 0, 0);
				list = g_list_append(list, dbmail_imap_plist_as_string(tlist));
				dbmail_imap_plist_free(tlist);
				tlist = NULL;
			} else
				list = g_list_append(list, g_strdup("NIL"));

			mime_findfield("content-language", header_to_use, &mr);
			if (mr && strlen(mr->value) > 0) {
				list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
			} else
				list = g_list_append(list, g_strdup("NIL"));
		}
	} else {
		/* check for a multipart message */
		if (is_rfc_multipart || is_mime_multipart) {
			curr = dm_list_getstart(&msg->children);
			tmp = g_string_new("");
			while (curr) {
				tlist = _imap_get_structure((mime_message_t *) curr->data, show_extension_data);
				
				pstring = dbmail_imap_plist_as_string(tlist);
				
				g_string_append_printf(tmp, "%s", pstring);
				
				g_free(pstring);
				
				dbmail_imap_plist_free(tlist);
				tlist = NULL;
				
				curr = curr->nextnode;
			}
			list = g_list_append(list, g_strdup(tmp->str));

			/* show multipart subtype */
			if (is_mime_multipart)
				mime_findfield("content-type", &msg->mimeheader, &mr);
			else
				mime_findfield("content-type", &msg->rfcheader, &mr);

			subtype = strchr(mr->value, '/');
			extension = strchr(subtype, ';');

			if (!subtype)
				list = g_list_append(list, g_strdup("NIL"));
			else {
				if (!extension) {
					newline = strchr(subtype, '\n');
					if (!newline)
						return NULL;

					*newline = 0;
					list = g_list_append(list, dbmail_imap_astring_as_string(subtype + 1));
					*newline = '\n';
				} else {
					*extension = 0;
					list = g_list_append(list, dbmail_imap_astring_as_string(subtype + 1));
					*extension = ';';
				}
			}

			/* show extension data (after subtype) */
			if (extension && show_extension_data) {
				tlist = _imap_get_mime_parameters(mr, 0, 1);
				
				tmp = g_list_join(tlist," ");
				
				dbmail_imap_plist_free(tlist);
				tlist = NULL;
				
				list = g_list_append(list, g_strdup(tmp->str));

				/* FIXME: should give body-disposition & body-language here */
				list = g_list_append(list, g_strdup("NIL NIL"));
			}
		} else {
			/* ??? */
		}
	}
	g_string_free(tmp,1);
	return list;
}


/*
 * _imap_get_envelope()
 *
 * retrieves the body envelope of an RFC-822 msg
 *
 * returns GList of char * elements
 * 
 */
static GList * _imap_get_envelope(struct dm_list *rfcheader)
{
	struct mime_record *mr;
	int idx;
	GList * list = NULL;

	trace(TRACE_DEBUG,"%s,%s", __FILE__,__func__);
	
	mime_findfield("date", rfcheader, &mr);
	if (mr && strlen(mr->value) > 0) 
		list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
	else
		list = g_list_append(list, g_strdup("NIL"));

	mime_findfield("subject", rfcheader, &mr);
	if (mr && strlen(mr->value) > 0)
		list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
	else
		list = g_list_append(list, g_strdup("NIL"));

	/* now from, sender, reply-to, to, cc, bcc, in-reply-to fields;
	 * note that multiple mailaddresses are separated by ','
	 */
	GString *tmp = g_string_new("");
	for (idx = 0; envelope_items[idx]; idx++) {
		mime_findfield(envelope_items[idx], rfcheader, &mr);
		if (mr && strlen(mr->value) > 0) {
			list = g_list_append(list, dbmail_imap_plist_as_string(_imap_get_addresses(mr)));
		} else if (strcasecmp(envelope_items[idx], "reply-to") == 0) {
			mime_findfield("from", rfcheader, &mr);
			if (mr && strlen(mr->value) > 0) {
				list = g_list_append(list, dbmail_imap_plist_as_string(_imap_get_addresses(mr)));
			} else
				list = g_list_append(list, g_strdup("((NIL NIL \"nobody\" \"nowhere.nirgendwo\"))"));
		} else if (strcasecmp(envelope_items[idx], "sender") == 0) {
			mime_findfield("from", rfcheader, &mr);
			if (mr && strlen(mr->value) > 0) {
				list = g_list_append(list, dbmail_imap_plist_as_string(_imap_get_addresses(mr)));
			} else
				list = g_list_append(list, g_strdup("((NIL NIL \"nobody\" \"nowhere.nirgendwo\"))"));
		} else
			list = g_list_append(list,g_strdup("NIL"));
	}

	mime_findfield("in-reply-to", rfcheader, &mr);
	if (mr && strlen(mr->value) > 0) {
		list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
	} else
		list = g_list_append(list, g_strdup("NIL"));

	mime_findfield("message-id", rfcheader, &mr);
	if (mr && strlen(mr->value) > 0)
		list = g_list_append(list, dbmail_imap_astring_as_string(mr->value));
	else
		list = g_list_append(list, g_strdup("NIL"));
	g_string_free(tmp,1);
	return list;
}


/*
 * _imap_get_addresses()
 *
 * gives an address list
 */
static GList * _imap_get_addresses(struct mime_record *mr)
{
	int delimiter, i, inquote, start, has_split;
	char savechar;
	char *value;
	
	GList * list = NULL;
	GList * sublist = NULL;
	GString * tmp = g_string_new("");
		
	trace(TRACE_DEBUG,"%s,%s: [%s]", __FILE__,__func__, mr->value);
	
	/* find ',' to split up multiple addresses */
	delimiter = 0;

	do {
		sublist = NULL;
		start = delimiter;

		for (inquote = 0; mr->value[delimiter] && !(mr->value[delimiter] == ',' && !inquote); delimiter++)
			if (mr->value[delimiter] == '\"')
				inquote ^= 1;

		if (mr->value[delimiter])
			mr->value[delimiter] = 0;	/* replace ',' by NULL-termination */
		else
			delimiter = -1;	/* this will be the last one */

		/* the address currently being processed is now contained within
		 * &mr->value[start] 'till first '\0'
		 */

		/* possibilities for the mail address:
		 * - name <user@domain>
		 * - "name" <user@domain>
		 * - <user@domain>
		 * - user@domain
		 * - user@domain (Name)
		 * scan for '<' to determine which case we should be dealing with;
		 */

		for (i = start, inquote = 0; mr->value[i] && !(mr->value[i] == '<' && !inquote); i++)
			if (mr->value[i] == '\"')
				inquote ^= 1;

		if (mr->value[i]) {
			if (i > start + 2) {
				/* name is contained in &mr->value[start] untill &mr->value[i-2] */
				/* name might contain quotes */
				savechar = mr->value[i - 1];
				mr->value[i - 1] = '\0';	/* terminate string */
				if ( !g_str_has_prefix(&mr->value[start],"\"") && !g_str_has_suffix(&mr->value[start],"\"") )
					value=g_strdup_printf("\"%s\"", &mr->value[start]);
				else
					value=g_strdup(&mr->value[start]);
					
				trace(TRACE_DEBUG,"%s,%s: found [%s]", __FILE__, __func__, value);
				sublist = g_list_append(sublist, dbmail_imap_astring_as_string(value));
				g_free(value);
				

				mr->value[i - 1] = savechar;

			} else
				sublist = g_list_append(sublist, g_strdup("NIL"));

			start = i + 1;	/* skip to after '<' */
		} else
			sublist = g_list_append(sublist, g_strdup("NIL"));

		sublist = g_list_append(sublist, g_strdup("NIL"));	/* source route ?? smtp at-domain-list ?? */

		/*
		 * now display user domainname; &mr->value[start] is starting point 
		 */
		
		g_string_printf(tmp,"\"");
		// added a check for whitespace within the address (not good)
		for (i = start, has_split = 0; mr->value[i] && mr->value[i] != '>' && !isspace(mr->value[i]); i++) {
			if (mr->value[i] == '@') {
				tmp = g_string_append(tmp, "\" \"");
				has_split = 1;
			} else {
				if (mr->value[i] == '"')
					tmp = g_string_append(tmp, "\\");
				g_string_append_printf(tmp, "%c", mr->value[i]);
			}
		}

		if (!has_split)
			tmp = g_string_append(tmp, "\" \"\"");	/* '@' did not occur */
		else
			tmp = g_string_append(tmp, "\"");

		sublist = g_list_append(sublist,g_strdup(tmp->str));
		
		if (delimiter > 0) {
			mr->value[delimiter++] = ',';	/* restore & prepare for next iteration */
			while (isspace(mr->value[delimiter]))
				delimiter++;
		}
		list = g_list_append(list, dbmail_imap_plist_as_string(sublist));

	} while (delimiter > 0);
	
	g_list_foreach(sublist, (GFunc)g_free, NULL);
	g_list_free(sublist);
	sublist = NULL;
	g_string_free(tmp,1);
	return list;
}



/*
 * _imap_get_mime_parameters()
 *
 * get mime name/value pairs
 * 
 * return GList for conversion to plist
 * 
 * if force_subtype != 0 'NIL' will be outputted if no subtype is specified
 * if only_extension != 0 only extension data (after first ';') will be shown
 */
static GList * _imap_get_mime_parameters(struct mime_record *mr, int force_subtype, int only_extension)
{
	GList * list = NULL;
	GList * subl = NULL;
	GString * tmp = g_string_new("");
	char * tmp2 = g_new(char, 255);
	char * tmp3 = g_new(char, 255);
	
	int idx, delimiter, start, end;

	/* find first delimiter */
	for (delimiter = 0; mr->value[delimiter] && mr->value[delimiter] != ';'; delimiter++);

	/* are there non-whitespace chars after the delimiter?                    */
	/* looking for the case where the mime type ends with a ";"               */
	/* if it is of type "text" it must have a default character set generated */
	end = strlen(mr->value);
	for (start = delimiter + 1; (isspace(mr->value[start]) == 0 && start <= end); start++);
	end = start - delimiter - 1;
	start = 0;
	if (end && strstr(mr->value, "text"))
		start++;

	if (mr->value[delimiter])
		mr->value[delimiter] = 0;
	else
		delimiter = -1;

	if (!only_extension) {
		/* find main type in value */
		for (idx = 0; mr->value[idx] && mr->value[idx] != '/';
		     idx++);

		if (mr->value[idx] && (idx < delimiter || delimiter == -1)) {
			mr->value[idx] = 0;
			list = g_list_append(list,dbmail_imap_astring_as_string(mr->value));
			list = g_list_append(list,dbmail_imap_astring_as_string(&mr->value[idx + 1]));

			mr->value[idx] = '/';
		} else {
			list = g_list_append(list,dbmail_imap_astring_as_string(mr->value));
			list = g_list_append(list, g_strdup(force_subtype ? "NIL" : ""));
		}
	}
	if (delimiter >= 0) {
		/* extra parameters specified */
		mr->value[delimiter] = ';';
		idx = delimiter;

		if (start)
			subl = g_list_append(subl, g_strdup("\"CHARSET\" \"US-ASCII\""));
		/* extra params: <name>=<val> [; <name>=<val> [; ...etc...]]
		 * note that both name and val may or may not be enclosed by 
		 * either single or double quotation marks
		 */

		do {
			/* skip whitespace */
			for (idx++; isspace(mr->value[idx]); idx++);
			if (!mr->value[idx])
				break;	/* ?? */

			/* check if quotation marks are specified */
			if (mr->value[idx] == '\"' || mr->value[idx] == '\'') {
				start = ++idx;
				while (mr->value[idx] && mr->value[idx] != mr->value[start - 1])
					idx++;

				if (!mr->value[idx] || mr->value[idx + 1] != '=')	/* ?? no end quote */
					break;

				end = idx;
				idx += 2;	/* skip to after '=' */
			} else {
				start = idx;
				while (mr->value[idx] && mr->value[idx] != '=')
					idx++;

				if (!mr->value[idx])	/* ?? no value specified */
					break;

				end = idx;
				idx++;	/* skip to after '=' */
			}

			subl = g_list_append_printf(subl, "\"%.*s\"", (end - start), &mr->value[start]);

			/* now process the value; practically same procedure */

			if (mr->value[idx] == '\"' || mr->value[idx] == '\'') {
				start = ++idx;
				while (mr->value[idx] && mr->value[idx] != mr->value[start - 1])
					idx++;

				if (!mr->value[idx])	/* ?? no end quote */
					break;

				end = idx;
				idx++;
			} else {
				start = idx;

				while (mr->value[idx] && !isspace(mr->value[idx]) && mr->value[idx] != ';')
					idx++;

				end = idx;
			}

			snprintf(tmp2,255,"\"%.*s\"", (end - start), &mr->value[start]);
			mime_unwrap(tmp3,tmp2);
			subl = g_list_append_printf(subl, tmp3);

			/* check for more name/val pairs */
			while (mr->value[idx] && mr->value[idx] != ';')
				idx++;

		} while (mr->value[idx]);

		list = g_list_append(list, dbmail_imap_plist_as_string(subl));
		g_list_foreach(subl, (GFunc)g_free, NULL);
		g_list_free(subl);
		subl = NULL;
		g_string_free(tmp,1);
	} else {
		list = g_list_append(list, g_strdup("NIL"));
	}
	g_free(tmp2);
	g_free(tmp3);

	return list;
}




static int _imap_session_fetch_parse_partspec(struct ImapSession *self, int idx)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	char *token, *nexttoken;

	token=self->args[idx];
	nexttoken=self->args[idx+1];

	trace(TRACE_DEBUG,"%s,%s: token [%s], nexttoken [%s]",__FILE__, __func__, token, nexttoken);

	for (j = 0; token[j]; j++) {
		if (isdigit(token[j])) {
			indigit = 1;
			continue;
		} else if (token[j] == '.') {
			if (!indigit)
				/* error, single dot specified */
				return -2;
			indigit = 0;
			continue;
		} else
			break;	/* other char found */
	}
	
	
	if (j > 0) {
		if (indigit && token[j])
			return -2;	/* error DONE */
		/* partspecifier present, save it */
		if (j >= IMAP_MAX_PARTSPEC_LEN)
			return -2;	/* error DONE */
		dbmail_imap_session_bodyfetch_set_partspec(self, token, j);
	}

	char *partspec = &token[j];


	int shouldclose = 0;
	if (MATCH(partspec, "text")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT);
		shouldclose = 1;
	} else if (MATCH(partspec, "header")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER);
		shouldclose = 1;
	} else if (MATCH(partspec, "mime")) {
		if (j == 0)
			return -2;	/* error DONE */

		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_MIME);
		shouldclose = 1;
	} else if (MATCH(partspec, "header.fields")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS);
	} else if (MATCH(partspec, "header.fields.not")) {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS_NOT);
	} else if (token[j] == '\0') {
		dbmail_imap_session_bodyfetch_set_itemtype(self, BFIT_TEXT_SILENT);
		shouldclose = 1;
	} else {
		return -2;	/* error DONE */
	}
	if (shouldclose) {
		if (! MATCH(nexttoken, "]"))
			return -2;	/* error DONE */
	} else {
		idx++;	/* should be at '(' now */
		token = self->args[idx];
		nexttoken = self->args[idx+1];
		
		if (! MATCH(token,"("))
			return -2;	/* error DONE */

		idx++;	/* at first item of field list now, remember idx */
		dbmail_imap_session_bodyfetch_set_argstart(self, idx);

		/* walk on untill list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(self->args[idx],")") )
			idx++;

		token = self->args[idx];
		nexttoken = self->args[idx+1];
		
		dbmail_imap_session_bodyfetch_set_argcnt(self, idx);

		if (dbmail_imap_session_bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;	/* error DONE */
	}
	return idx + 1;
}

static int _imap_session_fetch_parse_octet_range(struct ImapSession *self, int idx) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = self->args[idx];
	
	trace(TRACE_DEBUG,"%s,%s: parse token [%s]",__FILE__, __func__, token);

	if (token && token[0] == '<') {

		/* check argument */
		if (token[strlen(token) - 1] != '>')
			return -2;	/* error DONE */

		delimpos = -1;
		for (j = 1; j < strlen(token) - 1; j++) {
			if (token[j] == '.') {
				if (delimpos != -1) 
					return -2;
				delimpos = j;
			} else if (!isdigit (token[j]))
				return -2;
		}
		if (delimpos == -1 || delimpos == 1 || delimpos == (int) (strlen(token) - 2))
			return -2;	/* no delimiter found or at first/last pos OR invalid args DONE */

		/* read the numbers */
		token[strlen(token) - 1] = '\0';
		token[delimpos] = '\0';
		dbmail_imap_session_bodyfetch_set_octetstart(self, strtoll(&token[1], NULL, 10));
		dbmail_imap_session_bodyfetch_set_octetcnt(self,strtoll(&token [delimpos + 1], NULL, 10));

		/* restore argument */
		token[delimpos] = '.';
		token[strlen(token) - 1] = '>';
	} else {
		return idx;
	}

	return idx + 1;	/* DONE */
}

/*
 * dbmail_imap_session_fetch_parse_args()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int dbmail_imap_session_fetch_parse_args(struct ImapSession * self, int idx)
{
	int invalidargs, ispeek = 0;
	
	invalidargs = 0;

	if (!self->args[idx])
		return -1;	/* no more */

	if (self->args[idx][0] == '(')
		idx++;

	if (!self->args[idx])
		return -2;	/* error */

	
	char *token = NULL, *nexttoken = NULL;
	
	token = self->args[idx];
	nexttoken = self->args[idx+1];

	trace(TRACE_DEBUG,"%s,%s: parse args[%d] = [%s]",
		__FILE__,__func__, idx, token);

	if (MATCH(token,"flags")) {
		self->fi->getFlags = 1;
	} else if (MATCH(token,"internaldate")) {
		self->fi->getInternalDate=1;
	} else if (MATCH(token,"uid")) {
		self->fi->getUID=1;
	} else if (MATCH(token,"rfc822")) {
		self->fi->getRFC822=1;
	
	/* from here on message parsing will be necessary */
	
	} else if (MATCH(token,"rfc822.header")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Header = 1;
	} else if (MATCH(token,"rfc822.peek")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Peek = 1;
	} else if (MATCH(token,"rfc822.size")) {
		self->fi->msgparse_needed=1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"rfc822.text")) {
		self->fi->msgparse_needed=1;
		self->fi->getRFC822Text = 1;
	
	} else if (MATCH(token,"body") || MATCH(token,"body.peek")) {

		dbmail_imap_session_bodyfetch_new(self);

		self->fi->msgparse_needed=1;
		if (MATCH(token,"body.peek"))
			ispeek=1;
		
		nexttoken = (char *)self->args[idx+1];
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek)
				return -2;	/* error DONE */
			self->fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			/* now read the argument list to body */
			idx++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			idx++;	/* now pointing at what should be the item type */

			token = (char *)self->args[idx];
			nexttoken = (char *)self->args[idx+1];

			if (MATCH(token,"]")) {
				if (ispeek)
					self->fi->getBodyTotalPeek = 1;
				else
					self->fi->getBodyTotal = 1;
				return _imap_session_fetch_parse_octet_range(self,idx+1);
			}
			
			if (ispeek)
				self->fi->noseen = 1;

			
			idx = _imap_session_fetch_parse_partspec(self,idx);
			if (idx < 0) {
				trace(TRACE_DEBUG,"%s,%s: fetch_parse_partspec return with error", __FILE__, __func__);
				return -2;
			}
			/* idx points to ']' now */
			return _imap_session_fetch_parse_octet_range(self,idx+1);
		}
	} else if (MATCH(token,"all")) {		
		self->fi->msgparse_needed=1;
		self->fi->getFlags = 1;
		self->fi->getInternalDate = 1;
		self->fi->getSize = 1;
		self->fi->getEnvelope = 1;
	} else if (MATCH(token,"fast")) {
		self->fi->getFlags = 1;
		self->fi->getInternalDate = 1;
		self->fi->getSize = 1;
	} else if (MATCH(token,"full")) {
		self->fi->msgparse_needed=1;
		self->fi->getFlags = 1;
		self->fi->getInternalDate = 1;
		self->fi->getSize = 1;
		self->fi->getEnvelope = 1;
		self->fi->getMIME_IMB = 1;
	} else if (MATCH(token,"bodystructure")) {
		self->fi->msgparse_needed=1;
		self->fi->getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		self->fi->msgparse_needed=1;
		self->fi->getEnvelope = 1;
	} else {			
		if ((! nexttoken) && (strcmp(token,")") == 0)) {
			/* only allowed if last arg here */
			return -1;
		}
		return -2;	/* DONE */
	}
	trace(TRACE_DEBUG, "%s,%s: args[idx = %d] = %s (returning %d)\n",
	      __FILE__,__func__, idx, self->args[idx], idx + 1);
	return idx + 1;
}

int dbmail_imap_session_fetch_get_unparsed(struct ImapSession *self, u64_t fetch_start, u64_t fetch_end)
{

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	/* all the info we need can be retrieved by a single
	 * call to db_get_msginfo_range()
	 */

	u64_t lo, hi;
	u64_t i;
	int j;
	int result;
	unsigned nmatching;
	unsigned fn;

	GList *list = NULL;
	GList *sublist = NULL;
	GString *tmp = g_string_new("");
	
	trace(TRACE_DEBUG, "%s,%s: no parsing at all", __FILE__, __func__ );
	
	if (!self->use_uid) {
		/* find the msgUID's to use */
		lo = ud->mailbox.seq_list[fetch_start];
		hi = ud->mailbox.seq_list[fetch_end];

	} else {
		lo = fetch_start;
		hi = fetch_end;
	}

	/* (always retrieve uid) */
	if ((result = dbmail_imap_session_get_msginfo_range(self, lo, hi)) <= 0)
		return result;

	nmatching = (unsigned)result;
	
	trace(TRACE_DEBUG, "%s,%s: into non-parsing fetch loop for [%u] messages", __FILE__, __func__, nmatching);

	for (i = 0; i < nmatching; i++) {
		if (self->fi->getSize && self->msginfo[i].rfcsize == 0) {
			/* parse the message to calc the size */
			result = db_fetch_headers(self->msginfo[i].uid, &self->headermsg);
			if (result == -2) {
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
			if (result == -3) {
				dbmail_imap_session_printf(self, "\r\n* BYE out of memory\r\n");
				return -1;
			}

			self->msginfo[i].rfcsize = (self->headermsg.rfcheadersize +
			     self->headermsg.bodysize + self->headermsg.bodylines);
			db_set_rfcsize(self->msginfo[i].rfcsize, self->msginfo[i].uid, ud->mailbox.uid);
		}

		if (binary_search (ud->mailbox.seq_list, ud->mailbox.exists, self->msginfo[i].uid, &fn) == -1) {
			/* this is probably some sync error:
			 * the msgUID belongs to this mailbox but was not found
			 * when building the mailbox info
			 * let's call it fatal and let the client re-connect :)
			 */
			dbmail_imap_session_printf(self, "* BYE internal syncing error\r\n");
			return -1;
		}

		// start building the output
		
		list = NULL;
		sublist = NULL;
		tmp = g_string_new("");
		
		/* fetching results */
		if (self->fi->getInternalDate) 
			list = g_list_append_printf(list,"INTERNALDATE \"%s\"", date_sql2imap (self->msginfo[i].internaldate));

		if (self->fi->getUID || self->use_uid) 
			list = g_list_append_printf(list,"UID %llu", self->msginfo[i].uid);

		if (self->fi->getSize) 
			list = g_list_append_printf(list,"RFC822.SIZE %llu", self->msginfo[i].rfcsize);

		if (self->fi->getFlags) {
			sublist = NULL;
			for (j = 0; j < IMAP_NFLAGS; j++) {
				if (self->msginfo[i].flags[j]) 
					sublist = g_list_append(sublist,g_strdup((gchar *)imap_flag_desc_escaped[j]));
			}
			list = g_list_append_printf(list,"FLAGS %s", dbmail_imap_plist_as_string(sublist));
		}
		dbmail_imap_session_printf(self, "* %u FETCH %s\r\n", (fn + 1), dbmail_imap_plist_as_string(list));
	}
	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sublist,(GFunc)g_free,NULL);
	g_list_free(list);
	g_list_free(sublist);
	g_string_free(tmp,TRUE);
	return 0;
}

int dbmail_imap_session_get_msginfo_range(struct ImapSession *self, u64_t msg_idnr_low, u64_t msg_idnr_high)
{
	unsigned nrows, i, j;
	const char *query_result;
	char *to_char_str;
	msginfo_t *result;
	
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	db_free_result();

	to_char_str = date2char_str("internal_date");
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT seen_flag, answered_flag, deleted_flag, flagged_flag, "
		 "draft_flag, recent_flag, %s, rfcsize, message_idnr "
		 "FROM %smessages msg, %sphysmessage pm "
		 "WHERE pm.id = msg.physmessage_id "
		 "AND message_idnr BETWEEN '%llu' AND '%llu' "
		 "AND mailbox_idnr = '%llu' AND status < '%d' "
		 "ORDER BY message_idnr ASC",to_char_str,DBPFX,DBPFX,
		 msg_idnr_low, msg_idnr_high, ud->mailbox.uid,
		 MESSAGE_STATUS_DELETE);
	dm_free(to_char_str);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select message",
		      __FILE__, __func__);
		return (-1);
	}

	if ((nrows = db_num_rows()) == 0) {
		db_free_result();
		return 0;
	}

	if (! (result = g_new0(msginfo_t, nrows))) { 
		trace(TRACE_ERROR, "%s,%s: out of memory", __FILE__, __func__);
		db_free_result();
		return -2;
	}

	for (i = 0; i < nrows; i++) {
		if (self->fi->getFlags) {
			for (j = 0; j < IMAP_NFLAGS; j++)
				result[i].flags[j] = db_get_result_bool(i, j);
		}

		if (self->fi->getInternalDate) {
			query_result = db_get_result(i, IMAP_NFLAGS);
			strncpy(result[i].internaldate,
				(query_result) ? query_result :
				"1970-01-01 00:00:01",
				IMAP_INTERNALDATE_LEN);
		}
		if (self->fi->getSize) {
			result[i].rfcsize = db_get_result_u64(i, IMAP_NFLAGS + 1);
		}
		
		result[i].uid = db_get_result_u64(i, IMAP_NFLAGS + 2);
	}
	db_free_result();
	dbmail_imap_session_setMsginfo(self, result);

	return (int)nrows;
}


int dbmail_imap_session_fetch_get_items(struct ImapSession *self)
{
	int insert_rfcsize = 0, result;
	char date[IMAP_INTERNALDATE_LEN];
	u64_t actual_cnt, tmpdumpsize, rfcsize = 0;
	
	
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	GList *tlist = NULL;
	GString *tmp = g_string_new("");
	
	/* check RFC822.SIZE request */
	if (self->fi->getSize) {
		/* ok, try to fetch size from dbase */
		if (db_get_rfcsize (self->msg_idnr, ud->mailbox.uid, &rfcsize) == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
			return -1;
		}
		if (rfcsize == 0) {
			/* field is empty in dbase, message needs to be parsed */
			self->fi->msgparse_needed = 1;
			insert_rfcsize = 1;
		}
	}


	/* update cache */
	if (self->fi->msgparse_needed && self->msg_idnr != cached_msg.num) {
		if (! self->fi->msgparse_needed) {
			/* don't update cache if only the main header is needed 
			 * but do retrieve this main header
			 */

			result = db_get_main_header(self->msg_idnr, &self->headermsg.rfcheader);

			if (result == -1) {
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}

			if (result == -2) {
				dbmail_imap_session_printf(self, "\r\n* BYE out of memory\r\n");
				return -1;
			}

			if (result == -3) {
				/* parse error */
				g_string_free(tmp,TRUE);
				return 0;
			}

		} else {
			/* parse message structure */
			if (cached_msg.msg_parsed)
				db_free_msg(&cached_msg.msg);

			memset(&cached_msg.msg, 0, sizeof(cached_msg.msg));

			cached_msg.msg_parsed = 0;
			cached_msg.num = -1;
			cached_msg.file_dumped = 0;
			mreset(cached_msg.memdump);

			result = db_fetch_headers (self->msg_idnr, &cached_msg.msg);
			if (result == -2) {
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
			if (result == -3) {
				dbmail_imap_session_printf(self, "\r\n* BYE out of memory\r\n");
				return -1;
			}

			cached_msg.msg_parsed = 1;
			cached_msg.num = self->msg_idnr;

			rfcsize = (cached_msg.msg.rfcheadersize + cached_msg.msg.bodysize +
			     cached_msg.msg.bodylines);

			if (insert_rfcsize) {
				/* insert the rfc822 size into the dbase */
				if (db_set_rfcsize(rfcsize,self->msg_idnr,ud->mailbox.uid) == -1) {
					dbmail_imap_session_printf(self,"\r\n* BYE internal dbase error\r\n");
					return -1;
				}

				insert_rfcsize = 0;
			}

		}
	}

	self->fi->isfirstfetchout = 1;
	
	if (self->fi->getInternalDate) {
		result = db_get_msgdate(ud->mailbox.uid, self->msg_idnr, date);
		if (result == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
			return -1;
		}

		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		dbmail_imap_session_printf(self, "INTERNALDATE \"%s\"", date_sql2imap(date));
	}
	if (self->fi->getMIME_IMB) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		tlist = _imap_get_structure(&cached_msg.msg, 1);
		if (dbmail_imap_session_printf(self, "BODYSTRUCTURE %s", dbmail_imap_plist_as_string(tlist)) == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body structure\r\n");
			return -1;
		}
		g_list_foreach(tlist,(GFunc)g_free,NULL);
		g_list_free(tlist);
	}

	if (self->fi->getMIME_IMB_noextension) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		tlist = _imap_get_structure(&cached_msg.msg, 0);
		if (dbmail_imap_session_printf(self, "BODY %s", dbmail_imap_plist_as_string(tlist)) == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching body\r\n");
			return -1;
		}
		g_list_foreach(tlist,(GFunc)g_free,NULL);
		g_list_free(tlist);
	}

	if (self->fi->getEnvelope) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		tlist = _imap_get_envelope(&cached_msg.msg.rfcheader);
		if (dbmail_imap_session_printf(self, "ENVELOPE %s", dbmail_imap_plist_as_string(tlist)) == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE error fetching envelope structure\r\n");
			return -1;
		}
		g_list_foreach(tlist,(GFunc)g_free,NULL);
		g_list_free(tlist);
	}

	if (self->fi->getRFC822 || self->fi->getRFC822Peek) {
		if (cached_msg.file_dumped == 0 || cached_msg.num != self->msg_idnr) {
			mreset(cached_msg.memdump);

			cached_msg.dumpsize = rfcheader_dump(cached_msg.memdump,
			     &cached_msg.msg.rfcheader, self->args, 0, 0);

			cached_msg.dumpsize += db_dump_range(cached_msg.memdump,
			     cached_msg.msg.bodystart, cached_msg.msg.bodyend, self->msg_idnr);

			cached_msg.file_dumped = 1;

			if (cached_msg.num != self->msg_idnr) {
				/* if there is a parsed msg in the cache it will be invalid now */
				if (cached_msg.msg_parsed) {
					cached_msg.msg_parsed = 0;
					db_free_msg (&cached_msg.msg);
				}
				cached_msg.num = self->msg_idnr;
			}
		}

		mseek(cached_msg.memdump, 0, SEEK_SET);

		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		dbmail_imap_session_printf(self, "RFC822 {%llu}\r\n", cached_msg.dumpsize);
		send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);

		if (self->fi->getRFC822)
			self->fi->setseen = 1;

	}

	if (self->fi->getSize) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		dbmail_imap_session_printf(self, "RFC822.SIZE %llu", rfcsize);
	}

	if (self->fi->getBodyTotal || self->fi->getBodyTotalPeek) {
		if (cached_msg.file_dumped == 0 || cached_msg.num != self->msg_idnr) {
			cached_msg.dumpsize = rfcheader_dump(cached_msg.memdump,
			     &cached_msg.msg.rfcheader, self->args, 0, 0);

			cached_msg.dumpsize += db_dump_range(cached_msg.memdump,
			     cached_msg.msg.bodystart, cached_msg.msg.bodyend, self->msg_idnr);

			if (cached_msg.num != self->msg_idnr) {
				/* if there is a parsed msg in the cache it will be invalid now */
				if (cached_msg.msg_parsed) {
					cached_msg.msg_parsed = 0;
					db_free_msg(&cached_msg.msg);
				}
				cached_msg.num = self->msg_idnr;
			}

			cached_msg.file_dumped = 1;
		}

		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		if (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) == 0) {
			mseek(cached_msg.memdump, 0, SEEK_SET);

			dbmail_imap_session_printf(self, "BODY[] {%llu}\r\n", cached_msg.dumpsize);
			send_data(self->ci->tx, cached_msg.memdump, cached_msg.dumpsize);
		} else {
			mseek(cached_msg.memdump, dbmail_imap_session_bodyfetch_get_last_octetstart(self), SEEK_SET);

     /** \todo this next statement is ugly because of the
  casts to 'long long'. Probably, octetcnt should be
  changed to be a u64_t instead of a long long, because
  it should never be negative anyway */
			actual_cnt = (dbmail_imap_session_bodyfetch_get_last_octetcnt(self) >
			     (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)))
			    ? (((long long)cached_msg.dumpsize) - dbmail_imap_session_bodyfetch_get_last_octetstart(self)) 
			    : dbmail_imap_session_bodyfetch_get_last_octetcnt(self);

			dbmail_imap_session_printf(self, "BODY[]<%llu> {%llu}\r\n", 
					dbmail_imap_session_bodyfetch_get_last_octetstart(self), actual_cnt);
			send_data(self->ci->tx, cached_msg.memdump, actual_cnt);
		}

		if (self->fi->getBodyTotal)
			self->fi->setseen = 1;

	}

	if (self->fi->getRFC822Header) {
		/* here: msgparse_needed == 1
		 * if this msg is in cache, retrieve it from there
		 * otherwise only_main_header_parsing == 1 so retrieve direct
		 * from the dbase
		 */
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		if (cached_msg.num == self->msg_idnr) {
			mrewind(cached_msg.tmpdump);
			tmpdumpsize = rfcheader_dump(cached_msg.tmpdump, 
					&cached_msg.msg.rfcheader, self->args, 0, 0);
			mseek(cached_msg.tmpdump, 0, SEEK_SET);

			dbmail_imap_session_printf(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
			send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);
		} else {
			mrewind(cached_msg.tmpdump);
			tmpdumpsize = rfcheader_dump(cached_msg.tmpdump,
					&self->headermsg.rfcheader, self->args, 0, 0);
			mseek(cached_msg.tmpdump, 0, SEEK_SET);

			dbmail_imap_session_printf(self, "RFC822.HEADER {%llu}\r\n", tmpdumpsize);
			send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);
		}
	}

	if (self->fi->getRFC822Text) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		mrewind(cached_msg.tmpdump);
		tmpdumpsize = db_dump_range(cached_msg.tmpdump, cached_msg.msg.
				  bodystart, cached_msg.msg.bodyend, self->msg_idnr);
		mseek(cached_msg.tmpdump, 0, SEEK_SET);

		dbmail_imap_session_printf(self, "RFC822.TEXT {%llu}\r\n", tmpdumpsize);
		send_data(self->ci->tx, cached_msg.tmpdump, tmpdumpsize);

		self->fi->setseen = 1;
	}

	_imap_show_body_sections(self);

	/* set \Seen flag if necessary; note the absence of an error-check 
	 * for db_get_msgflag()!
	 */
	int setSeenSet[IMAP_NFLAGS] = { 1, 0, 0, 0, 0, 0 };
	if (self->fi->setseen && db_get_msgflag("seen", self->msg_idnr, ud->mailbox.uid) != 1) {
		/* only if the user has an ACL which grants
		   him rights to set the flag should the
		   flag be set! */
		result = acl_has_right(ud->userid, ud->mailbox.uid, ACL_RIGHT_SEEN);
		if (result == -1) {
			dbmail_imap_session_printf(self, "\r\n *BYE internal dbase error\r\n");
			return -1;
		}
		
		if (result == 1) {
			result = db_set_msgflag(self->msg_idnr, ud->mailbox.uid, setSeenSet, IMAPFA_ADD);
			if (result == -1) {
				dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
				return -1;
			}
		}

		self->fi->getFlags = 1;
		dbmail_imap_session_printf(self, " ");
	}

	/* FLAGS ? */
	if (self->fi->getFlags) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		int j;	
		int msgflags[IMAP_NFLAGS];
		result = db_get_msgflag_all(self->msg_idnr, ud->mailbox.uid, msgflags);
		if (result == -1) {
			dbmail_imap_session_printf(self, "\r\n* BYE internal dbase error\r\n");
			return -1;
		}
		tlist = NULL;
		for (j = 0; j < IMAP_NFLAGS; j++) {
			if (msgflags[j]) {
				tlist = g_list_append(tlist,g_strdup((gchar *)imap_flag_desc_escaped[j]));
			}

		}
		dbmail_imap_session_printf(self,"FLAGS %s", dbmail_imap_plist_as_string(tlist));
	}

	if (self->fi->getUID) {
		if (self->fi->isfirstfetchout)
			self->fi->isfirstfetchout = 0;
		else
			dbmail_imap_session_printf(self, " ");

		dbmail_imap_session_printf(self, "UID %llu", self->msg_idnr);
	}

	dbmail_imap_session_printf(self, ")\r\n");
	g_string_free(tmp,TRUE);
	return 0;
}
	
static void _imap_show_body_sections(struct ImapSession *self) {
	dbmail_imap_session_bodyfetch_rewind(self);
	g_list_foreach(self->fi->bodyfetch,(GFunc)_imap_show_body_section, (gpointer)self);
	dbmail_imap_session_bodyfetch_rewind(self);
}

static int _imap_show_body_section(body_fetch_t *bodyfetch, gpointer data) {

	int only_text_from_msgpart = 0;
	long long cnt;
	u64_t tmpdumpsize;
	mime_message_t *msgpart;
	GList *tlist = NULL;
	int k;
	struct ImapSession *self = (struct ImapSession *)data;
	
	if (bodyfetch->itemtype < 0)
		return 0;
	
	trace(TRACE_DEBUG,"%s,%s: itemtype [%d] partspec [%s]", __FILE__, __func__, bodyfetch->itemtype, bodyfetch->partspec);
	
	mrewind(cached_msg.tmpdump);
	
	if (bodyfetch->partspec[0]) {
		if (bodyfetch->partspec[0] == '0') {
			dbmail_imap_session_printf(self, "\r\n%s BAD protocol error\r\n", self->tag);
			trace(TRACE_DEBUG, "PROTOCOL ERROR");
			return 1;
		}

		msgpart = get_part_by_num(&cached_msg.msg, bodyfetch->partspec);

		if (!msgpart) {
			/* if the partspec ends on "1" or "1." the msg body
			 * of the parent message is to be retrieved
			 */
			int partspeclen;
			partspeclen = strlen(bodyfetch->partspec);

			if ((bodyfetch->partspec[partspeclen - 1] == '1' && 
				(partspeclen == 1 || bodyfetch->partspec[partspeclen - 2] == '.')
				)
			    || ((bodyfetch->partspec[partspeclen - 1] == '.'
			      && bodyfetch->partspec[partspeclen - 2] == '1')
			     && (partspeclen == 2 || bodyfetch->partspec[partspeclen - 3] == '.'))
			    ) {
				/* ok find the parent of this message */
				/* start value of k is partspeclen-2 'cause we could
				   have partspec[partspeclen-1] == '.' right at the start
				 */
				int k;
				for (k = partspeclen - 2; k >= 0; k--)
					if (bodyfetch->partspec[k] == '.')
						break;

				if (k > 0) {
					bodyfetch->partspec[k] = '\0';
					msgpart = get_part_by_num(&cached_msg.msg, bodyfetch->partspec);
					bodyfetch->partspec[k] = '.';
				} else
					msgpart = &cached_msg.msg;

				only_text_from_msgpart = 1;
			}
		} else {
			only_text_from_msgpart = 0;
		}
	} else {
		if (cached_msg.num == self->msg_idnr)
			msgpart = &cached_msg.msg;
		else {
			/* this will be only the case when only_main_header_parsing == 1 */
			msgpart = &self->headermsg;
		}
	}

	if (self->fi->isfirstfetchout)
		self->fi->isfirstfetchout = 0;
	else
		dbmail_imap_session_printf(self, " ");

	if (! self->fi->noseen)
		self->fi->setseen = 1;
	dbmail_imap_session_printf(self, "BODY[%s", bodyfetch->partspec);

	switch (bodyfetch->itemtype) {
	case BFIT_TEXT_SILENT:
		if (!msgpart)
			dbmail_imap_session_printf(self, "] NIL ");
		else {
			tmpdumpsize = 0;

			if (!only_text_from_msgpart)
				tmpdumpsize = rfcheader_dump(cached_msg.tmpdump,
				     &msgpart->rfcheader, self->args, 0, 0);

			tmpdumpsize += db_dump_range(cached_msg.tmpdump,
			     msgpart->bodystart, msgpart->bodyend, self->msg_idnr);

			if (bodyfetch->octetcnt > 0) {
				cnt = get_dumpsize(bodyfetch, tmpdumpsize);
				dbmail_imap_session_printf(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
				mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
			} else {
				cnt = tmpdumpsize;
				dbmail_imap_session_printf(self, "] {%llu}\r\n", tmpdumpsize);
				mseek(cached_msg.tmpdump, 0, SEEK_SET);
			}

			/* output data */
			send_data(self->ci->tx, cached_msg.tmpdump, cnt);

		}
		break;

	case BFIT_TEXT:
		/* dump body text */
		dbmail_imap_session_printf(self, "TEXT");
		if (!msgpart)
			dbmail_imap_session_printf(self, "] NIL ");
		else {
			tmpdumpsize = db_dump_range(cached_msg.tmpdump,
					msgpart->bodystart, msgpart->bodyend, self->msg_idnr);

			if (bodyfetch->octetcnt > 0) {
				cnt = get_dumpsize(bodyfetch,tmpdumpsize);
				dbmail_imap_session_printf(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart,cnt);
				mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
			} else {
				cnt = tmpdumpsize;
				dbmail_imap_session_printf(self, "] {%llu}\r\n", tmpdumpsize);
				mseek(cached_msg.tmpdump, 0, SEEK_SET);
			}
			/* output data */
			send_data(self->ci->tx, cached_msg.tmpdump, cnt);
		}
		break;

	case BFIT_HEADER:
		dbmail_imap_session_printf(self, "HEADER");
		if (!msgpart || only_text_from_msgpart)
			dbmail_imap_session_printf(self, "] NIL\r\n");
		else {
			tmpdumpsize = rfcheader_dump(cached_msg.tmpdump, 
					&msgpart->rfcheader, self->args, 0, 0);

			if (!tmpdumpsize) {
				dbmail_imap_session_printf(self, "] NIL\r\n");
			} else {
				if (bodyfetch->octetcnt > 0) {
					cnt = get_dumpsize(bodyfetch, tmpdumpsize);
					dbmail_imap_session_printf(self, "]<%llu> {%llu}\r\n", bodyfetch->octetstart,cnt);
					mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
				} else {
					cnt = tmpdumpsize;
					dbmail_imap_session_printf(self, "] {%llu}\r\n", tmpdumpsize);
					mseek(cached_msg.tmpdump,0,SEEK_SET);
				}
				/* output data */
				send_data(self->ci->tx,cached_msg.tmpdump,cnt);

			}
		}
		break;
		
	case BFIT_HEADER_FIELDS:
		
		tlist = NULL;
		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, g_strdup(self->args[k + bodyfetch->argstart]));

		dbmail_imap_session_printf(self,"HEADER.FIELDS %s", dbmail_imap_plist_as_string(tlist));
		dbmail_imap_session_printf(self, "] ");
		g_list_foreach(tlist, (GFunc)g_free, NULL);

		if (!msgpart || only_text_from_msgpart)
			dbmail_imap_session_printf(self, "NIL\r\n");
		else {
			tmpdumpsize = rfcheader_dump(cached_msg.tmpdump, 
					&msgpart->rfcheader, &self->args[bodyfetch->argstart], 
					bodyfetch->argcnt, 1);

			if (!tmpdumpsize) {
				dbmail_imap_session_printf(self, "NIL\r\n");
			} else {
				if (bodyfetch->octetcnt > 0) {
					cnt = get_dumpsize(bodyfetch, tmpdumpsize);
					dbmail_imap_session_printf(self, "<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
					mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
				} else {
					cnt = tmpdumpsize;
					dbmail_imap_session_printf(self, "{%llu}\r\n", tmpdumpsize);
					mseek(cached_msg.tmpdump, 0, SEEK_SET);
				}

				/* output data */
				send_data(self->ci->tx,cached_msg.tmpdump,cnt);

			}
		}
		break;
	case BFIT_HEADER_FIELDS_NOT:

		tlist = NULL;
		for (k = 0; k < bodyfetch->argcnt; k++) 
			tlist = g_list_append(tlist, g_strdup(self->args[k + bodyfetch->argstart]));

		dbmail_imap_session_printf(self, "HEADER.FIELDS.NOT %s", dbmail_imap_plist_as_string(tlist));
		dbmail_imap_session_printf(self, "] ");
		g_list_foreach(tlist,(GFunc)g_free,NULL);

		if (!msgpart || only_text_from_msgpart)
			dbmail_imap_session_printf(self, "NIL\r\n");
		else {
			tmpdumpsize = rfcheader_dump(cached_msg.tmpdump, 
					&msgpart->rfcheader, &self->args[bodyfetch->argstart],
					bodyfetch->argcnt, 0);

			if (!tmpdumpsize) {
				dbmail_imap_session_printf(self, "NIL\r\n");
			} else {
				if (bodyfetch->octetcnt > 0) {
					cnt = get_dumpsize(bodyfetch, tmpdumpsize);
					dbmail_imap_session_printf(self, "<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
					mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
				} else {
					cnt = tmpdumpsize;
					dbmail_imap_session_printf(self, "{%llu}\r\n", tmpdumpsize);
					mseek(cached_msg.tmpdump, 0, SEEK_SET);
				}
				/* output data */
				send_data(self->ci->tx, cached_msg.tmpdump, cnt);
			}
		}
		break;
	case BFIT_MIME:
		dbmail_imap_session_printf(self, "MIME] ");

		if (!msgpart)
			dbmail_imap_session_printf(self, "NIL\r\n");
		else {
			tmpdumpsize = mimeheader_dump(cached_msg.tmpdump, &msgpart->mimeheader);

			if (!tmpdumpsize) {
				dbmail_imap_session_printf(self, "NIL\r\n");
			} else {
				if (bodyfetch->octetcnt > 0) {
					cnt = get_dumpsize(bodyfetch, tmpdumpsize);
					dbmail_imap_session_printf(self, "<%llu> {%llu}\r\n", bodyfetch->octetstart, cnt);
					mseek(cached_msg.tmpdump, bodyfetch->octetstart, SEEK_SET);
				} else {
					cnt = tmpdumpsize;
					dbmail_imap_session_printf(self, "{%llu}\r\n", tmpdumpsize);
					mseek(cached_msg.tmpdump, 0, SEEK_SET);
				}

				/* output data */
				send_data(self->ci->tx, cached_msg.tmpdump, cnt);
			}
		}

		break;
	default:
		dbmail_imap_session_printf(self, "\r\n* BYE internal server error\r\n");
		return -1;
	}
	g_list_free(tlist);
	return 0;
}



/*
 * check_state_and_args()
 *
 * checks if the user is in the right state & the numbers of arguments;
 * a state of -1 specifies any state
 * arguments can be grouped by means of parentheses
 *
 * returns 1 on succes, 0 on failure
 */
int check_state_and_args(struct ImapSession * self, const char *command, int minargs, int maxargs, int state)
{
	int i;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	/* check state */
	if (state != -1) {
		if (ud->state != state) {
			if (!  (state == IMAPCS_AUTHENTICATED && ud->state == IMAPCS_SELECTED)) {
				dbmail_imap_session_printf(self,
					"%s BAD %s command received in invalid state\r\n",
					self->tag, command);
				return 0;
			}
		}
	}

	/* check args */
	for (i = 0; i < minargs; i++) {
		if (!self->args[i]) {
			/* error: need more args */
			dbmail_imap_session_printf(self,
				"%s BAD missing argument%s to %s\r\n", self->tag,
				(minargs == 1) ? "" : "(s)", command);
			return 0;
		}
	}

	for (i = 0; self->args[i]; i++);

	if (maxargs && (i > maxargs)) {
		/* error: too many args */
		dbmail_imap_session_printf(self, "%s BAD too many arguments to %s\r\n", self->tag,
			command);
		return 0;
	}

	/* succes */
	return 1;
}

int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...)
{
	int maxlen=100;
	int result = 0;
	gchar *re = g_new0(gchar, maxlen+1);
	va_list ap;
	
	FILE * fd = self->ci->tx;
	int len;	
	va_start(ap, message);
	if (feof(fd) || (len = vfprintf(fd,message,ap)) < 0 || fflush(fd) < 0) {
		va_end(ap);
		return -1;
	}
	va_end(ap);
	
	va_start(ap, message);
	result = vsnprintf(re,maxlen,message,ap);
	va_end(ap);
	
	if (result < maxlen)
		trace(TRACE_DEBUG,"RESPONSE: [%s]", re);
	else
		trace(TRACE_DEBUG,"RESPONSE: [%s...]", re);
	g_free(re);
	
	return len;
}
	
int dbmail_imap_session_readln(struct ImapSession * self, char * buffer)
{
	memset(buffer, 0, MAX_LINESIZE);
	alarm(self->ci->timeout);
	if (fgets(buffer, MAX_LINESIZE, self->ci->rx) == NULL) {
		trace(TRACE_ERROR, "%s,%s: error reading from client", __FILE__, __func__);
		dbmail_imap_session_printf(self, "* BYE Error reading input\r\n");
		return -1;
	}
	alarm(0);
	return strlen(buffer);
}
	
int dbmail_imap_session_handle_auth(struct ImapSession * self, char * username, char * password)
{

	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	timestring_t timestring;
	create_current_timestring(&timestring);
	
	u64_t userid = 0;
	
	trace(TRACE_DEBUG, "%s,%s: trying to validate user [%s], pass [%s]", 
			__FILE__, __func__, username, (password ? "XXXX" : "(null)") );
	
	int valid = auth_validate(self->ci, username, password, &userid);
	
	trace(TRACE_MESSAGE, "%s,%s: user (id:%llu, name %s) tries login",
			__FILE__, __func__, userid, username);

	if (valid == -1) {
		/* a db-error occurred */
		dbmail_imap_session_printf(self, "* BYE internal db error validating user\r\n");
		trace(TRACE_ERROR,
		      "%s,%s: db-validate error while validating user %s (pass %s).",
		      __FILE__, __func__, username, password);
		return -1;
	}

	if (valid == 0) {
		sleep(2);	/* security */

		/* validation failed: invalid user/pass combination */
		trace(TRACE_MESSAGE, "IMAPD [PID %d]: user (name %s) login rejected @ %s",
		      (int) getpid(), username, timestring);
		dbmail_imap_session_printf(self, "%s NO login rejected\r\n", self->tag);

		return 1;
	}

	/* login ok */
	trace(TRACE_MESSAGE, "%s,%s: user (id %llu, name %s) login accepted @ %s",
	      __FILE__, __func__, 
	      userid, username, timestring);

	/* update client info */
	ud->userid = userid;
	ud->state = IMAPCS_AUTHENTICATED;

	return 0;

}


int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value )
{
	char *buf;
	GString *tmp;
	tmp = g_string_new(prompt);
	
	if (! (buf = g_new0(char, sizeof(char) * MAX_LINESIZE))) {
		trace(TRACE_ERROR, "%s,%s: malloc failure", __FILE__, __func__);
		return -1;
	}
			
	tmp = g_string_append(tmp, "\r\n");
	base64encode(tmp->str, buf);

	dbmail_imap_session_printf(self, "+ %s\r\n", buf);
	fflush(self->ci->tx);
	
	if ( (dbmail_imap_session_readln(self, buf) < 0) )
		return -1;

	tmp = g_string_new(buf);
	base64decode(tmp->str, buf);

	memcpy(value,buf,strlen(buf));

	g_string_free(tmp,1);
	dm_free(buf);
	
	return 0;
}

u64_t dbmail_imap_session_mailbox_get_idnr(struct ImapSession * self, char * mailbox)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	u64_t uid;
	int i;
	
	/* remove trailing '/' if present */
	while (strlen(mailbox) > 0 && mailbox[strlen(mailbox) - 1] == '/')
		mailbox[strlen(mailbox) - 1] = '\0';

	/* remove leading '/' if present */
	for (i = 0; mailbox[i] && mailbox[i] == '/'; i++);
	memmove(&mailbox[0], &mailbox[i],
		(strlen(mailbox) - i) * sizeof(char));

	db_findmailbox(mailbox, ud->userid, &uid);
	
	return uid;
}

int dbmail_imap_session_mailbox_check_acl(struct ImapSession * self, u64_t idnr,  ACLRight_t acl)
{
	int access;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	access = acl_has_right(ud->userid, idnr, acl);
	if (access < 0) {
		dbmail_imap_session_printf(self, "* BYE internal database error\r\n");
		return -1;
	}
	if (access == 0) {
		dbmail_imap_session_printf(self, "%s NO permission denied\r\n", self->tag);
		dbmail_imap_session_set_state(self,IMAPCS_AUTHENTICATED);
		return 1;
	}
	return 0;
}

int dbmail_imap_session_mailbox_get_selectable(struct ImapSession * self, u64_t idnr)
{
	/* check if mailbox is selectable */
	int selectable;
	selectable = db_isselectable(idnr);
	if (selectable == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	if (selectable == 0) {
		dbmail_imap_session_printf(self, "%s NO specified mailbox is not selectable\r\n", self->tag);
		return 1;
	}
	return 0;
}

int dbmail_imap_session_mailbox_show_info(struct ImapSession * self) 
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	int result = db_getmailbox(&ud->mailbox);
	if (result == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;
	}
	/* msg counts */
	dbmail_imap_session_printf(self, "* %u EXISTS\r\n", ud->mailbox.exists);
	dbmail_imap_session_printf(self, "* %u RECENT\r\n", ud->mailbox.recent);

	GString *string;
	/* flags */
	GList *list = NULL;
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		list = g_list_append(list,"\\Seen");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		list = g_list_append(list,"\\Answered");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		list = g_list_append(list,"\\Deleted");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		list = g_list_append(list,"\\Flagged");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		list = g_list_append(list,"\\Draft");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		list = g_list_append(list,"\\Recent");
	string = g_list_join(list," ");
	g_list_free(list);
	dbmail_imap_session_printf(self, "* FLAGS (%s)\r\n", string->str);

	/* permanent flags */
	list = NULL;
	if (ud->mailbox.flags & IMAPFLAG_SEEN)
		list = g_list_append(list,"\\Seen");
	if (ud->mailbox.flags & IMAPFLAG_ANSWERED)
		list = g_list_append(list,"\\Answered");
	if (ud->mailbox.flags & IMAPFLAG_DELETED)
		list = g_list_append(list,"\\Deleted");
	if (ud->mailbox.flags & IMAPFLAG_FLAGGED)
		list = g_list_append(list,"\\Flagged");
	if (ud->mailbox.flags & IMAPFLAG_DRAFT)
		list = g_list_append(list,"\\Draft");
	if (ud->mailbox.flags & IMAPFLAG_RECENT)
		list = g_list_append(list,"\\Recent");
	string = g_list_join(list," ");
	g_list_free(list);
	dbmail_imap_session_printf(self, "* OK [PERMANENTFLAGS (%s)]\r\n", string->str);

	/* UIDNEXT */
	dbmail_imap_session_printf(self, "* OK [UIDNEXT %llu] Predicted next UID\r\n",
		ud->mailbox.msguidnext);
	
	/* UID */
	dbmail_imap_session_printf(self, "* OK [UIDVALIDITY %llu] UID value\r\n",
		ud->mailbox.uid);

	g_string_free(string,TRUE);
	return 0;
}
	
int dbmail_imap_session_mailbox_open(struct ImapSession * self, char * mailbox)
{
	int result;
	u64_t mailbox_idnr;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	
	/* get the mailbox_idnr */
	if (! (mailbox_idnr = dbmail_imap_session_mailbox_get_idnr(self, mailbox))) {
		ud->state = IMAPCS_AUTHENTICATED;
		dm_free(ud->mailbox.seq_list);
		memset(&ud->mailbox, 0, sizeof(ud->mailbox));
		dbmail_imap_session_printf(self, "%s NO specified mailbox does not exist\r\n", self->tag);
		return 1; /* error */
	}

	/* check if user has right to select mailbox */
	if ((result = dbmail_imap_session_mailbox_check_acl(self, mailbox_idnr, ACL_RIGHT_READ)))
		return result;
	
	/* check if mailbox is selectable */
	if ((result = dbmail_imap_session_mailbox_get_selectable(self, mailbox_idnr)))
		return result;
	
	ud->mailbox.uid = mailbox_idnr;
	
	/* read info from mailbox */ 
	if ((result = db_getmailbox(&ud->mailbox)) == -1) {
		dbmail_imap_session_printf(self, "* BYE internal dbase error\r\n");
		return -1;	/* fatal  */
	}

	return 0;
}

int dbmail_imap_session_mailbox_select_recent(struct ImapSession *self) {
	unsigned i, j;
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;

	self->recent = NULL;
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages WHERE recent_flag = 1 AND mailbox_idnr = '%llu'",
		 DBPFX, ud->mailbox.uid);

	if (db_query(query) == -1) 
		return (-1);

	j = db_num_rows();
	for (i = 0; i < j; i++) 
		self->recent = g_list_append(self->recent, g_strdup(db_get_result(i, 0)));
	
	db_free_result();
	trace(TRACE_DEBUG, "%s,%s: recent [%d] in mailbox [%llu]",
			__FILE__, __func__, g_list_length(self->recent), ud->mailbox.uid);

	return g_list_length(self->recent);
}

int dbmail_imap_session_mailbox_update_recent(struct ImapSession *self) {
	GList *slices = NULL;
	
	if (self->recent == NULL)
		return 0;

	slices = g_list_slices(self->recent,100);
	slices = g_list_first(slices);
	while (slices) {
		snprintf(query, DEF_QUERYSIZE, "update %smessages set recent_flag = 0 "
				"where message_idnr in (%s)", DBPFX, (gchar *)slices->data);
		if (db_query(query) == -1) 
			return (-1);
		if (! g_list_next(slices))
			break;
		slices = g_list_next(slices);
	}
	
	g_list_foreach(self->recent,(GFunc)g_free, NULL);
	g_list_free(self->recent);
	g_list_foreach(slices, (GFunc)g_free, NULL);
	g_list_free(slices);
	
	self->recent = NULL;
	return 0;
}
int dbmail_imap_session_set_state(struct ImapSession *self, int state)
{
	imap_userdata_t *ud = (imap_userdata_t *) self->ci->userData;
	switch (state) {
		case IMAPCS_AUTHENTICATED:
			ud->state = state;
			dm_free(ud->mailbox.seq_list);
			memset(&ud->mailbox, 0, sizeof(ud->mailbox));
			break;
			
		default:
			ud->state = state;
			break;
	}
	return 0;
}


/*****************************************************************************
 *
 *
 *   bodyfetch
 *
 *
 ****************************************************************************/

void dbmail_imap_session_bodyfetch_new(struct ImapSession *self) 
{
	body_fetch_t *bodyfetch = g_new0(body_fetch_t, 1);
	bodyfetch->itemtype = -1;
	self->fi->bodyfetch = g_list_append(self->fi->bodyfetch, bodyfetch);
}

void dbmail_imap_session_bodyfetch_rewind(struct ImapSession *self) 
{
	self->fi->bodyfetch = g_list_first(self->fi->bodyfetch);
}

void dbmail_imap_session_bodyfetch_free(struct ImapSession *self) 
{
	g_list_foreach(self->fi->bodyfetch, (GFunc)g_free, NULL);
	g_list_free(self->fi->bodyfetch);
}

body_fetch_t * dbmail_imap_session_bodyfetch_get_last(struct ImapSession *self) 
{
	if (self->fi->bodyfetch == NULL)
		dbmail_imap_session_bodyfetch_new(self);
	
	self->fi->bodyfetch = g_list_last(self->fi->bodyfetch);
	return (body_fetch_t *)self->fi->bodyfetch->data;
}

int dbmail_imap_session_bodyfetch_set_partspec(struct ImapSession *self, char *partspec, int length) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	memset(bodyfetch->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(bodyfetch->partspec,partspec,length);
	return 0;
}
char *dbmail_imap_session_bodyfetch_get_last_partspec(struct ImapSession *self) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->partspec;
}

int dbmail_imap_session_bodyfetch_set_itemtype(struct ImapSession *self, int itemtype) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->itemtype = itemtype;
	return 0;
}
int dbmail_imap_session_bodyfetch_get_last_itemtype(struct ImapSession *self) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->itemtype;
}
int dbmail_imap_session_bodyfetch_set_argstart(struct ImapSession *self, int idx) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argstart = idx;
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_get_last_argstart(struct ImapSession *self) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argstart;
}
int dbmail_imap_session_bodyfetch_set_argcnt(struct ImapSession *self, int idx) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->argcnt = idx - bodyfetch->argstart;
	return bodyfetch->argcnt;
}
int dbmail_imap_session_bodyfetch_get_last_argcnt(struct ImapSession *self) 
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->argcnt;
}

int dbmail_imap_session_bodyfetch_set_octetstart(struct ImapSession *self, guint64 octet)
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetstart = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(struct ImapSession *self)
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetstart;
}


int dbmail_imap_session_bodyfetch_set_octetcnt(struct ImapSession *self, guint64 octet)
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	bodyfetch->octetcnt = octet;
	return 0;
}
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(struct ImapSession *self)
{
	body_fetch_t *bodyfetch = dbmail_imap_session_bodyfetch_get_last(self);
	return bodyfetch->octetcnt;
}

u64_t get_dumpsize(body_fetch_t *bodyfetch, u64_t dumpsize) 
{
	long long cnt = dumpsize - bodyfetch->octetstart;
	if (cnt < 0)
		cnt = 0;
	if ((guint64)cnt > bodyfetch->octetcnt)
		cnt = bodyfetch->octetcnt;
	return (u64_t)cnt;
}

