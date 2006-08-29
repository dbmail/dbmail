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

/* $Id: dm_imaputil.c 1878 2005-09-04 06:34:44Z paul $
 * 
 * dm_imaputil.c
 *
 * IMAP-server utility functions implementations
 */


#include "dbmail.h"

#define THIS_MODULE "imap"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE (10*1024)
#endif

#define BUFLEN 2048
#define SEND_BUF_SIZE 1024
#define MAX_ARGS 512

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* cache */
extern cache_t cached_msg;

/* consts */
const char AcceptedChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? \n\r";

const char AcceptedTagChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "!@#$%^&-=_`~\\|'\" ;:,.<>/? ";

/* some basic imap type utils */
char *dbmail_imap_plist_collapse(const char *in)
{
	/*
	 * collapse "(NIL) (NIL)" to "(NIL)(NIL)"
	 *
	 * do for bodystructure, and only for addresslists in the envelope
	 */
	char *p;
	char **sublists;

	g_return_val_if_fail(in,NULL);
	
	sublists = g_strsplit(in,") (",0);
	p = g_strjoinv(")(",sublists);
	g_strfreev(sublists);
	return p;
}

/*
 *  build a parenthisized list (4.4) from a GList
 */
char *dbmail_imap_plist_as_string(GList * list)
{
	char *p;
	size_t l;
	GString * tmp1 = g_string_new("");
	GString * tmp2 = g_list_join(list, " ");
	g_string_printf(tmp1,"(%s)", tmp2->str);

	/*
	 * strip empty outer parenthesis
	 * "((NIL NIL))" to "(NIL NIL)" 
	 */
	p = tmp1->str;
	l = tmp1->len;
	while (tmp1->len>4 && p[0]=='(' && p[l-1]==')' && p[1]=='(' && p[l-2]==')') {
		tmp1 = g_string_truncate(tmp1,l-1);
		tmp1 = g_string_erase(tmp1,0,1);
		p=tmp1->str;
	}
	
	g_string_free(tmp1,FALSE);
	g_string_free(tmp2,TRUE);
	return p;
}

void dbmail_imap_plist_free(GList *l)
{
	g_list_foreach(l, (GFunc)g_free, NULL);
	g_list_free(l);
}

/* 
 * return a quoted or literal astring
 */

char *dbmail_imap_astring_as_string(const char *s)
{
	int i;
	char *r;
	char *t, *l = NULL;
	char first, last, penult = '\\';

	if (! s)
		return g_strdup("\"\"");

	l = g_strdup(s);
	t = l;
	/* strip off dquote */
	first = s[0];
	last = s[strlen(s)-1];
	if (strlen(s) > 2)
		penult = s[strlen(s)-2];
	if ((first == '"') && (last == '"') && (penult != '\\')) {
		l[strlen(l)-1] = '\0';
		l++;
	}
	
	for (i=0; l[i]; i++) { 
		if ((l[i] & 0x80) || (l[i] == '\r') || (l[i] == '\n') || (l[i] == '"') || (l[i] == '\\')) {
			r = g_strdup_printf("{%lu}\r\n%s", (unsigned long) strlen(l), l);
			g_free(t);
			return r;
		}
		
	}
	r = g_strdup_printf("\"%s\"", l);
	g_free(t);

	return r;

}
/* structure and envelope tools */
static void _structure_part_handle_part(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_text(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension);
static void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension);


static void get_param_list(gpointer key, gpointer value, gpointer data)
{
	gchar *s = g_mime_utils_header_encode_text((unsigned char *)((GMimeParam *)value)->value);
	*(GList **)data = g_list_append_printf(*(GList **)data, "\"%s\"", (char *)key);
	*(GList **)data = g_list_append_printf(*(GList **)data, "\"%s\"", s);
	g_free(s);
}

static GList * imap_append_hash_as_string(GList *list, GHashTable *hash)
{
	GList *l = NULL;
	char *s;
	if (hash) 
		g_hash_table_foreach(hash, get_param_list, (gpointer)&(l));
	if (l) {
		s = dbmail_imap_plist_as_string(l);
		list = g_list_append_printf(list, "%s", s);
		g_free(s);
		
		g_list_foreach(l,(GFunc)g_free,NULL);
		g_list_free(l);
	} else {
		list = g_list_append_printf(list, "NIL");
	}
	
	return list;
}
static GList * imap_append_disposition_as_string(GList *list, GMimeObject *part)
{
	GList *t = NULL;
	GMimeDisposition *disposition;
	char *result;
	const char *disp = g_mime_object_get_header(part, "Content-Disposition");
	
	if(disp) {
		disposition = g_mime_disposition_new(disp);
		t = g_list_append_printf(t,"\"%s\"",disposition->disposition);
		
		/* paramlist */
		t = imap_append_hash_as_string(t, disposition->param_hash);
		
		result = dbmail_imap_plist_as_string(t);
		list = g_list_append_printf(list,"%s",result);
		g_free(result);

		g_list_foreach(t,(GFunc)g_free,NULL);
		g_list_free(t);
		g_mime_disposition_destroy(disposition);
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	return list;
}

#define imap_append_header_as_string(list, part, header) \
	imap_append_header_as_string_default(list, part, header, "NIL")

static GList * imap_append_header_as_string_default(GList *list,
		GMimeObject *part, const char *header, const char *def)
{
	char *result;
	char *s;
	if((result = (char *)g_mime_object_get_header(part, header))) {
		s = dbmail_imap_astring_as_string(result);
		list = g_list_append_printf(list, "%s", s);
		g_free(s);
	} else {
		list = g_list_append_printf(list, def);
	}
	return list;
}

static void imap_part_get_sizes(GMimeObject *part, size_t * size, size_t * lines)
{
	char *v, *h, *t;
	GString *b;
	int i;
	size_t s = 0, l = 0;

	/* get encoded size */
	h = g_mime_object_get_headers(part);
	t = g_mime_object_to_string(part);
	b = g_string_new(t);
	g_free(t);
	
	s = strlen(h);
	if (b->len > s)
		s++;
	
	b = g_string_erase(b,0,s);
	t = get_crlf_encoded(b->str);
	s = strlen(t);
	
	/* count body lines */
	v = t;
	i = 0;
	while (v[i++]) {
		if (v[i]=='\n')
			l++;
	}
	if (s >=2 && v[s-2] != '\n')
		l++;
	
	g_free(h);
	g_free(t);
	g_string_free(b,TRUE);
	*size = s;
	*lines = l;
}


void _structure_part_handle_part(GMimeObject *part, gpointer data, gboolean extension)
{
	const GMimeContentType *type;
	GMimeObject *object;

	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type) {
		if (GMIME_IS_MESSAGE(part))
			g_object_unref(object);
		return;
	}

	/* multipart composite */
	if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(object,data, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(object,data, extension);
	/* simple message */
	else
		_structure_part_text(object,data, extension);

	if (GMIME_IS_MESSAGE(part))
		g_object_unref(object);
		
}

void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension)
{
	GMimeMultipart *multipart;
	GMimeObject *subpart, *object;
	GList *list = NULL;
	GList *alist = NULL;
	GString *s;
	int i,j;
	const GMimeContentType *type;
	gchar *b;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		if (GMIME_IS_MESSAGE(part)) g_object_unref(object);
		return;
	}
	multipart = GMIME_MULTIPART(object);
	i = g_mime_multipart_get_number(multipart);
	
	b = g_mime_content_type_to_string(type);
	trace(TRACE_DEBUG,"%s,%s: parse [%d] parts for [%s] with boundary [%s]",
			__FILE__, __func__, 
			i, b, g_mime_multipart_get_boundary(multipart));
	g_free(b);

	/* loop over parts for base info */
	for (j=0; j<i; j++) {
		subpart = g_mime_multipart_get_part(multipart,j);
		_structure_part_handle_part(subpart,&alist,extension);
		g_object_unref(subpart);
	}
	
	/* sub-type */
	alist = g_list_append_printf(alist,"\"%s\"", type->subtype);

	/* extension data (only for multipart, in case of BODYSTRUCTURE command argument) */
	if (extension) {
		/* paramlist */
		list = imap_append_hash_as_string(list, type->param_hash);
		/* disposition */
		list = imap_append_disposition_as_string(list, object);
		/* language */
		list = imap_append_header_as_string(list,object,"Content-Language");
		/* location */
		list = imap_append_header_as_string(list,object,"Content-Location");
		s = g_list_join(list," ");
		
		alist = g_list_append(alist,s->str);

		g_list_foreach(list,(GFunc)g_free,NULL);
		g_list_free(list);
		g_string_free(s,FALSE);
	}

	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(alist));
	
	g_list_foreach(alist,(GFunc)g_free,NULL);
	g_list_free(alist);
	if (GMIME_IS_MESSAGE(part)) g_object_unref(object);

	
}
void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result, *b;
	GList *list = NULL;
	size_t s, l=0;
	GMimeObject *object;
	const GMimeContentType *type;
	GMimeMessage *tmpmes;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		if (GMIME_IS_MESSAGE(part)) g_object_unref(object);
		return;
	}
	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, type->param_hash);
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(object)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,object,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string_default(list,object,"Content-Transfer-Encoding", "\"7BIT\"");
	/* body size */
	imap_part_get_sizes(object,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);

	/* envelope structure */
	b = imap_get_envelope(tmpmes = g_mime_message_part_get_message(GMIME_MESSAGE_PART(part)));
	list = g_list_append_printf(list,"%s", b?b:"NIL");
	g_object_unref(tmpmes);
	g_free(b);

	/* body structure */
	b = imap_get_structure(tmpmes = g_mime_message_part_get_message(GMIME_MESSAGE_PART(part)), extension);
	list = g_list_append_printf(list,"%s", b?b:"NIL");
	g_object_unref(tmpmes);
	g_free(b);

	/* lines */
	list = g_list_append_printf(list,"%d", l);
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(list));
	
	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_free(list);
	if (GMIME_IS_MESSAGE(part)) g_object_unref(object);

}
void _structure_part_text(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result;
	GList *list = NULL;
	size_t s, l=0;
	GMimeObject *object;
	const GMimeContentType *type;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type){
		if (GMIME_IS_MESSAGE(part)) g_object_unref(object);
		return;
	}
	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, type->param_hash);
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(object)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,object,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string_default(list,object,"Content-Transfer-Encoding", "\"7BIT\"");
	/* body size */
	imap_part_get_sizes(part,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);
	
	/* body lines */
	if (g_mime_content_type_is_type(type,"text","*"))
		list = g_list_append_printf(list,"%d", l);
	
	/* extension data in case of BODYSTRUCTURE */
	if (extension) {
		/* body md5 */
		list = imap_append_header_as_string(list,object,"Content-MD5");
		/* body disposition */
		list = imap_append_disposition_as_string(list,object);
		/* body language */
		list = imap_append_header_as_string(list,object,"Content-Language");
		/* body location */
		list = imap_append_header_as_string(list,object,"Content-Location");
	}
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data, dbmail_imap_plist_as_string(list));
	
	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_free(list);
	if (GMIME_IS_MESSAGE(part)) g_object_unref(object);
}



GList* dbmail_imap_append_alist_as_plist(GList *list, const InternetAddressList *ialist)
{
	GList *t = NULL, *p = NULL;
	InternetAddress *ia = NULL;
	InternetAddressList *ial;
	gchar *s = NULL, *st = NULL;
	gchar **tokens;
	gchar *name;
	gchar *mailbox;

	if (ialist==NULL)
		return g_list_append_printf(list, "NIL");

	ial = (InternetAddressList *)ialist;
	while(ial->address) {
		
		ia = ial->address;
		g_return_val_if_fail(ia!=NULL, list);

		switch (ia->type) {
		case INTERNET_ADDRESS_NONE:
			TRACE(TRACE_DEBUG, "nothing doing.");
			break;

		case INTERNET_ADDRESS_GROUP:
			TRACE(TRACE_DEBUG, "recursing into address group [%s].", ia->name);
			
			/* Address list beginning. */
			p = g_list_append_printf(p, "(NIL NIL \"%s\" NIL)", ia->name);

			/* Dive into the address list.
			 * Careful, this builds up the stack; it's not a tail call.
			 */
			t = dbmail_imap_append_alist_as_plist(t, ia->value.members);

			s = dbmail_imap_plist_as_string(t);
			/* Only use the results if they're interesting --
			 * (NIL) is the special case of nothing inside the group.
			 */
			if (strcmp(s, "(NIL)") != 0) {
				/* Lop off the extra parens at each end.
				 * Really do the pointer math carefully.
				 */
				size_t slen = strlen(s);
				if (slen) slen--;
				s[slen] = '\0';
				p = g_list_append_printf(p, "%s", (slen ? s+1 : s));
			}
			g_free(s);
			
			g_list_foreach(t, (GFunc)g_free, NULL);
			g_list_free(t);
			t = NULL;

			/* Address list ending. */
			// p = g_list_append_printf(p, "(NIL NIL NIL NIL)", ia->name);

			break;

		case INTERNET_ADDRESS_NAME:
			TRACE(TRACE_DEBUG, "handling a standard address [%s] [%s].", ia->name, ia->value.addr);

			/* personal name */
			if (ia->name && ia->value.addr) {
				name = g_mime_utils_header_encode_phrase((unsigned char *)ia->name);
				s = dbmail_imap_astring_as_string(name);
				t = g_list_append_printf(t, "%s", s);
				g_free(name);
				g_free(s);
			} else {
				t = g_list_append_printf(t, "NIL");
			}
                        
			/* source route */
			t = g_list_append_printf(t, "NIL");
                        
			/* mailbox name and host name */
			if ((mailbox = ia->value.addr ? ia->value.addr : ia->name) != NULL) {
				/* defensive mode for 'To: "foo@bar.org"' addresses */
				g_strstrip(g_strdelimit(mailbox,"\"",' '));
				
				tokens = g_strsplit(mailbox,"@",2);
                        
				/* mailbox name */
				if (tokens[0])
					t = g_list_append_printf(t, "\"%s\"", tokens[0]);
				else
					t = g_list_append_printf(t, "NIL");
				/* host name */
				if (tokens[1])
					t = g_list_append_printf(t, "\"%s\"", tokens[1]);
				else
					t = g_list_append_printf(t, "NIL");
				
				g_strfreev(tokens);
			} else {
				t = g_list_append_printf(t, "NIL NIL");
			}
			
			s = dbmail_imap_plist_as_string(t);
			p = g_list_append_printf(p, "%s", s);
			g_free(s);
			
			g_list_foreach(t, (GFunc)g_free, NULL);
			g_list_free(t);
			t = NULL;

			break;
		}
	
		/* Bottom of the while loop.
		 * Advance the address list.
		 */
		if (ial->next == NULL)
			break;
		
		ial = ial->next;
	}
	
	/* Tack it onto the outer list. */
	if (p) {
		s = dbmail_imap_plist_as_string(p);
		st = dbmail_imap_plist_collapse(s);
		list = g_list_append_printf(list, "(%s)", st);
		g_free(s);
		g_free(st);
        
		g_list_foreach(p, (GFunc)g_free, NULL);
		g_list_free(p);
	} else {
		list = g_list_append_printf(list, "NIL");
	}

	return list;
}

/* structure access point */
char * imap_get_structure(GMimeMessage *message, gboolean extension) 
{
	GList *structure = NULL;
	GMimeContentType *type;
	GMimeObject *part;
	char *s, *t;
	
	part = g_mime_message_get_mime_part(message);
	type = (GMimeContentType *)g_mime_object_get_content_type(part);
	if (! type) {
		trace(TRACE_DEBUG,"%s,%s: error getting content_type",
				__FILE__, __func__);
		g_object_unref(part);
		return NULL;
	}
	
	s = g_mime_content_type_to_string(type);
	trace(TRACE_DEBUG,"%s,%s: message type: [%s]", __FILE__, __func__, s);
	g_free(s);
	
	/* multipart composite */
	if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(part,(gpointer)&structure, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(part,(gpointer)&structure, extension);
	/* as simple message */
	else
		_structure_part_text(part,(gpointer)&structure, extension);
	
	s = dbmail_imap_plist_as_string(structure);
	t = dbmail_imap_plist_collapse(s);
	g_free(s);

	g_list_foreach(structure,(GFunc)g_free,NULL);
	g_list_free(structure);
	g_object_unref(part);
	
	return t;
}

GList * envelope_address_part(GList *list, GMimeMessage *message, const char *header)
{
	char *result, *t;
	InternetAddressList *alist;
	
	result = (char *)g_mime_message_get_header(message,header);
	if (result) {
		t = imap_cleanup_address(result);
		alist = internet_address_parse_string(t);
		g_free(t);
		list = dbmail_imap_append_alist_as_plist(list, (const InternetAddressList *)alist);
		internet_address_list_destroy(alist);
		alist = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	return list;
}
	

/* envelope access point */
char * imap_get_envelope(GMimeMessage *message)
{
	GMimeObject *part;
	GList *list = NULL;
	char *result;
	char *s, *t;

	if (! GMIME_IS_MESSAGE(message))
		return NULL;
	
	part = GMIME_OBJECT(message);
	/* date */
	result = g_mime_message_get_date_string(message);
	if (result) {
		t = dbmail_imap_astring_as_string(result);
		list = g_list_append_printf(list,"%s", t);
		g_free(result);
		g_free(t);
		result = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* subject */
	result = (char *)g_mime_message_get_subject(message);
	if (result) {
		t = dbmail_imap_astring_as_string(result);
		list = g_list_append_printf(list,"%s", t);
		g_free(t);
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* from */
	list = envelope_address_part(list, message, "From");
	/* sender */
	if (g_mime_message_get_header(message,"Sender"))
		list = envelope_address_part(list, message, "Sender");
	else
		list = envelope_address_part(list, message, "From");

	/* reply-to */
	if (g_mime_message_get_header(message,"Reply-to"))
		list = envelope_address_part(list, message, "Reply-to");
	else
		list = envelope_address_part(list, message, "From");
		
	/* to */
	list = envelope_address_part(list, message, "To");
	/* cc */
	list = envelope_address_part(list, message, "Cc");
	/* bcc */
	list = envelope_address_part(list, message, "Bcc");
	
	/* in-reply-to */
	list = imap_append_header_as_string(list,part,"In-Reply-to");
	/* message-id */
	result = (char *)g_mime_message_get_message_id(message);
	if (result && (! g_strrstr(result,"="))) {
                t = g_strdup_printf("<%s>", result);
		s = dbmail_imap_astring_as_string(t);
		list = g_list_append_printf(list,"%s", s);
		g_free(s);
                g_free(t);
	} else {
		list = g_list_append_printf(list,"NIL");
	}

	s = dbmail_imap_plist_as_string(list);

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_free(list);
	
	return s;
}


char * imap_get_logical_part(const GMimeObject *object, const char * specifier) 
{
	gchar *t=NULL;
	GString *s = g_string_new("");
	
	if (strcasecmp(specifier,"HEADER")==0 || strcasecmp(specifier,"MIME")==0) {
		t = g_mime_object_get_headers(GMIME_OBJECT(object));
		g_string_printf(s,"%s\n", t);
		g_free(t);
	} 
	
	else if (strcasecmp(specifier,"TEXT")==0) {
		t = g_mime_object_get_body(GMIME_OBJECT(object));
		g_string_printf(s,"%s",t);
		g_free(t);
	}

	t = s->str;
	g_string_free(s,FALSE);
	return t;
}

	

GMimeObject * imap_get_partspec(const GMimeObject *message, const char *partspec) 
{
	GMimeObject *object;
	GMimeContentType *type;
	char *part;
	guint index;
	guint i;
	
	GString *t = g_string_new(partspec);
	GList *specs = g_string_split(t,".");
	g_string_free(t,TRUE);
	
	object = GMIME_OBJECT(message);
	for (i=0; i< g_list_length(specs); i++) {
		part = g_list_nth_data(specs,i);
		if (! (index = strtol((const char *)part, NULL, 0))) 
			break;
		
		if (i==0 && GMIME_IS_MESSAGE(object))
			object=GMIME_OBJECT(GMIME_MESSAGE(object)->mime_part);
		
		type = (GMimeContentType *)g_mime_object_get_content_type(object);
		if (g_mime_content_type_is_type(type,"message","rfc822")) {
			object=GMIME_OBJECT(GMIME_MESSAGE_PART(object)->message);
			assert(object);
			continue;
		}
		if (g_mime_content_type_is_type(type,"multipart","*")) {
			object=g_mime_multipart_get_part((GMimeMultipart *)object, (int)index-1);
			assert(object);
			g_object_unref(object);
			continue;
		}
	}
	return object;
}

/* get headers or not */
static GTree * _fetch_headers(const GList *ids, const GList *headers, gboolean not)
{
	unsigned i=0, rows=0;
	GString *r, *h, *q = g_string_new("");
	gchar *pid, *fld, *val, *old, *new;
	GTree *t;

	r = g_list_join((GList *)ids,",");
	h = g_list_join((GList *)headers,"','");
	h = g_string_ascii_down(h);
	
	g_string_printf(q,"SELECT physmessage_id,headername,headervalue "
			"FROM %sheadervalue v "
			"JOIN %sheadername n ON v.headername_id=n.id "
			"WHERE physmessage_id IN (%s) "
			"AND lower(headername) %s IN ('%s')",
			DBPFX, DBPFX, r->str, not?"NOT":"", h->str);
	
	g_string_free(r,TRUE);
	g_string_free(h,TRUE);
	
	if (db_query(q->str)==-1) {
		g_string_free(q,TRUE);
		return NULL;
	}
	
	t = g_tree_new_full((GCompareDataFunc)strcmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	rows = db_num_rows();
	for(i=0;i<rows;i++) {
		
		pid = (char *)db_get_result(i,0);
		fld = (char *)db_get_result(i,1);
		val = (char *)db_get_result(i,2);
		
		old = g_tree_lookup(t, (gconstpointer)pid);
		new = g_strdup_printf("%s%s: %s\n", old?old:"", fld, val);
		
		g_tree_insert(t,g_strdup(pid),new);
	}
	db_free_result();
	g_string_free(q,TRUE);
	
	return t;
}


char * imap_message_fetch_headers(u64_t physid, const GList *headers, gboolean not)
{
	gchar *tmp, *res = NULL;
	gchar *id = g_strdup_printf("%llu",physid);
	GList *ids = g_list_append_printf(NULL, "%s", id);
	GTree *tree = _fetch_headers(ids,headers,not);
	
	tmp = (char *)g_tree_lookup(tree,id);
	if (tmp)
		res = g_strdup(tmp);
	
	g_list_foreach(ids,(GFunc)g_free,NULL);
	g_list_free(ids);
	g_free(id);
	g_tree_destroy(tree);
	
	return res;
}

char * imap_cleanup_address(const char *a) 
{
	/* 
	 * one ugly hack to work around a problem where gmime is too strict 
	 * in it's parsing of addresses
	 *
	 * FIXME: doesn't work for addresslists
	 */
	char *r, *t;
	char *inptr;
	char prev;
	GString *s = g_string_new("");
	
	t = g_strdup(a);
	inptr = t;
	inptr = g_strstrip(inptr);
	prev = *inptr;
	
	// quote encoded string
	if (*inptr == '=') {
		g_string_append_c(s,'"');
		
		while (*inptr && *inptr != ' ' && *inptr != '\t' && *inptr!='<') {
			if (*inptr == '"') {
				inptr++;
				continue;
			}
			g_string_append_c(s,*inptr);
			prev = *inptr;
			inptr++;
		}
		if (prev == '=' && *inptr)
			g_string_append_c(s,'"');

		if (*inptr == '<')
			g_string_append_c(s,' ');
	}
		
	if (*inptr)
		g_string_append(s,inptr);
	g_free(t);
	
	if (g_str_has_suffix(s->str,";"))
		s = g_string_truncate(s,s->len-1);

	r = s->str;
	g_string_free(s,FALSE);
	return r;
}

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
 * checkchars()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkchars(const char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		if (!strchr(AcceptedChars, s[i])) {
			/* wrong char found */
			return 0;
		}
	}
	return 1;
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


/*
 * binary_search()
 *
 * performs a binary search on array to find key
 * array should be ascending in values
 *
 * returns -1 if not found. key_idx will hold key if found
 */
int binary_search(const u64_t * array, unsigned arraysize, u64_t key,
		  unsigned int *key_idx)
{
	unsigned low, high, mid = 1;

	assert(key_idx != NULL);
	*key_idx = 0;
	if (arraysize == 0)
		return -1;

	low = 0;
	high = arraysize - 1;

	while (low <= high) {
		mid = (high + low) / (unsigned) 2;
		if (array[mid] < key)
			low = mid + 1;
		else if (array[mid] > key) {
			if (mid > 0)
				high = mid - 1;
			else
				break;
		} else {
			*key_idx = mid;
			return 1;
		}
	}

	return -1;		/* not found */
}

/*
 * send_data()
 *
 * sends cnt bytes from a MEM structure to a FILE stream
 * uses a simple buffering system
 */
void send_data(FILE * to, MEM * from, int cnt)
{
	char buf[SEND_BUF_SIZE];

	for (cnt -= SEND_BUF_SIZE; cnt >= 0; cnt -= SEND_BUF_SIZE) {
		mread(buf, SEND_BUF_SIZE, from);
		fwrite(buf, SEND_BUF_SIZE, 1, to);
	}

	if (cnt < 0) {
		mread(buf, cnt + SEND_BUF_SIZE, from);
		fwrite(buf, cnt + SEND_BUF_SIZE, 1, to);
	}

	fflush(to);
}



/* 
 * init cache 
 */
int init_cache()
{
	int serr;
	cached_msg.dmsg = NULL;
	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	if (! (cached_msg.memdump = mopen())) {
		serr = errno;
		trace(TRACE_ERROR,"%s,%s: mopen() failed [%s]",
				__FILE__, __func__, strerror(serr));
		errno = serr;
		return -1;
	}

	
	if (! (cached_msg.tmpdump = mopen())) {
		serr = errno;
		trace(TRACE_ERROR,"%s,%s: mopen() failed [%s]",
				__FILE__, __func__, strerror(serr));
		errno = serr;
		mclose(&cached_msg.memdump);
		return -1;
	}

	cached_msg.file_dumped = 0;
	cached_msg.dumpsize = 0;
	return 0;
}
/*
 * closes the msg cache
 */
void close_cache()
{
	if (cached_msg.dmsg)
		dbmail_message_free(cached_msg.dmsg);

	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	mclose(&cached_msg.memdump);
	mclose(&cached_msg.tmpdump);
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




