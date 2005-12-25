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

/* $Id: dm_imaputil.c 1878 2005-09-04 06:34:44Z paul $
 * 
 * dm_imaputil.c
 *
 * IMAP-server utility functions implementations
 */


#include "dbmail.h"

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

char base64encodestring[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const char *item_desc[] = { "TEXT", "HEADER", "MIME", "HEADER.FIELDS", "HEADER.FIELDS.NOT" };

const char *envelope_items[] = { "from", "sender", "reply-to", "to", "cc", "bcc", NULL };

static const char *search_cost[] = { "b","b","c","c","c","b","d","d","d","c","e","e","b","b","j","j","j" };

/* some basic imap type utils */
char *dbmail_imap_plist_collapse(const char *in)
{
	/*
	 * collapse "(NIL) (NIL)" to "(NIL)(NIL)"
	 *
	 * do for bodystructure, don't for envelope
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
			g_free(l);
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
	*(GList **)data = g_list_append_printf(*(GList **)data, "\"%s\"", (char *)key);
	*(GList **)data = g_list_append_printf(*(GList **)data, "\"%s\"", ((GMimeParam *)value)->value);
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
		if (disposition->param_hash)
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
static GList * imap_append_header_as_string(GList *list, GMimeObject *part, const char *header)
{
	char *result;
	if((result = (char *)g_mime_object_get_header(part,header))) {
		list = g_list_append_printf(list,"\"%s\"",result);
	} else {
		list = g_list_append_printf(list,"NIL");
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
	if (v[s-2] != '\n')
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
	if (! type)
		return;

	/* multipart composite */
	if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(object,data, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(object,data, extension);
	/* simple message */
	else
		_structure_part_text(object,data, extension);

}

void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension)
{
	GMimeMultipart *multipart;
	GMimeObject *subpart, *object;
	GList *list = NULL;
	GString *s;
	int i,j;
	const GMimeContentType *type;
	gchar *b;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type)
		return;

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
		_structure_part_handle_part(subpart,data,extension);
	}
	
	/* sub-type */
	*(GList **)data = g_list_append_printf(*(GList **)data,"\"%s\"", type->subtype);

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
		
		*(GList **)data = (gpointer)g_list_append(*(GList **)data,s->str);

		g_list_foreach(list,(GFunc)g_free,NULL);
		g_list_free(list);
		g_string_free(s,FALSE);
	}
	
}
void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result, *b;
	GList *list = NULL;
	size_t s, l=0;
	GMimeObject *object;
	const GMimeContentType *type;
	
	if (GMIME_IS_MESSAGE(part))
		object = g_mime_message_get_mime_part(GMIME_MESSAGE(part));
	else
		object = part;
	
	type = g_mime_object_get_content_type(object);
	if (! type)
		return;

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
	list = imap_append_header_as_string(list,object,"Content-Transfer-Encoding");
	/* body size */
	imap_part_get_sizes(object,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);

	/* envelope structure */
	b = imap_get_envelope(GMIME_MESSAGE(part));
	list = g_list_append_printf(list,"%s", b);
	g_free(b);

	/* body structure */
	b = imap_get_structure(GMIME_MESSAGE(part), extension);
	list = g_list_append_printf(list,"%s", b);
	g_free(b);

	/* lines */
	list = g_list_append_printf(list,"%d", l);
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(list));
	
	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_free(list);

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
	if (! type)
		return;

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
	list = imap_append_header_as_string(list,object,"Content-Transfer-Encoding");
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
}



static GList * _imap_append_alist_as_plist(GList *list, const InternetAddressList *ialist)
{
	GList *t = NULL, *p = NULL;
	InternetAddress *ia = NULL;
	InternetAddressList *ial;
	char *s = NULL;
	char **tokens;

	if (ialist==NULL)
		return g_list_append_printf(list, "NIL");

	ial = (InternetAddressList *)ialist;
	while(ial->address) {
		
		ia = ial->address;
		g_return_val_if_fail(ia!=NULL, list);
		
		/* personal name */
		if (ia->name)
			t = g_list_append_printf(t, "\"%s\"", ia->name);
		else
			t = g_list_append_printf(t, "NIL");

		/* source route */
		t = g_list_append_printf(t, "NIL");

		/* mailbox name */
		if (ia->value.addr) {
			tokens = g_strsplit(ia->value.addr,"@",2);

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
	
		if (ial->next == NULL)
			break;
		
		ial = ial->next;
	
	}
	
	if (p) {
		s = dbmail_imap_plist_as_string(p);
		list = g_list_append_printf(list, "(%s)", s);
		g_free(s);

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
	if (! type)
		return NULL;
	
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
	
	return t;
}

/* envelope access point */
char * imap_get_envelope(GMimeMessage *message)
{
	GMimeObject *part;
	InternetAddressList *alist;
	GList *list = NULL;
	char *result;
	char *s;

	if (! GMIME_IS_MESSAGE(message))
		return NULL;
	
	part = GMIME_OBJECT(message);
	/* date */
	result = g_mime_message_get_date_string(message);
	if (result) {
		list = g_list_append_printf(list,"\"%s\"", result);
		g_free(result);
		result = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* subject */
	result = (char *)g_mime_message_get_subject(message);
	if (result) {
		list = g_list_append_printf(list,"\"%s\"", result);
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* from */
	result = (char *)g_mime_message_get_header(message,"From");
	if (result) {
		alist = internet_address_parse_string(result);
		list = _imap_append_alist_as_plist(list, (const InternetAddressList *)alist);
		internet_address_list_destroy(alist);
		alist = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* sender */
	if (! (result = (char *)g_mime_message_get_header(message,"Sender")))
		result = (char *)g_mime_message_get_header(message,"From");
	if (result) {
		alist = internet_address_parse_string(result);
		list = _imap_append_alist_as_plist(list, alist);
		internet_address_list_destroy(alist);
		alist = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}

	/* reply-to */
	if (! (result = (char *)g_mime_message_get_header(message,"Reply-to")))
		result = (char *)g_mime_message_get_header(message,"From");
	if (result) {
		alist = internet_address_parse_string(result);
		list = _imap_append_alist_as_plist(list, alist);
		internet_address_list_destroy(alist);
		alist = NULL;
	} else {
		list = g_list_append_printf(list,"NIL");
	}
	
	/* to */
	alist = (InternetAddressList *)g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_TO);
	list = _imap_append_alist_as_plist(list, alist);
	
	/* cc */
	alist = (InternetAddressList *)g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_CC);
	list = _imap_append_alist_as_plist(list, alist);
	
	/* bcc */
	alist = (InternetAddressList *)g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_BCC);
	list = _imap_append_alist_as_plist(list, alist);
	
	/* in-reply-to */
	list = imap_append_header_as_string(list,part,"In-Reply-to");
	/* message-id */
	result = (char *)g_mime_message_get_message_id(message);
	if (result)
		list = g_list_append_printf(list,"\"<%s>\"", result);
	else
		list = g_list_append_printf(list,"NIL");

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
			object=GMIME_OBJECT(GMIME_MESSAGE(object)->mime_part);
			assert(object);
			continue;
		}
		if (g_mime_content_type_is_type(type,"multipart","*")) {
			object=g_mime_multipart_get_part((GMimeMultipart *)object, (int)index-1);
			assert(object);
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

/* 
 * sort_search()
 * 
 * reorder searchlist by using search cost as the sort key
 * 
 */ 

int sort_search(struct dm_list *searchlist) 
{
	struct element *el;
	search_key_t *left = NULL, *right = NULL, *tmp=NULL;
	
	if (!searchlist) 
		return 0;

	el = dm_list_getstart(searchlist);
	while (el != NULL) {
		if (! el->nextnode)
			break;
		left = el->data;
		right = (el->nextnode)->data;

		/* recurse into sub_search if necessary */
		switch(left->type) {
			case IST_SUBSEARCH_AND:
			case IST_SUBSEARCH_NOT:
			case IST_SUBSEARCH_OR:
				sort_search(&left->sub_search);
				break;
				;;
		}
		/* switch elements to sorted order */
		if (strcmp((char *)search_cost[left->type],(char *)search_cost[right->type]) > 0) {
			tmp = el->data;
			el->data = el->nextnode->data;
			el->nextnode->data = tmp;
			/* when in doubt, use brute force: starting over */
			el = dm_list_getstart(searchlist);
			continue;
		}
		
		el = el->nextnode;
	}
	return 0;
}


/* 
 * find a string in an array of strings
 */
int haystack_find(int haystacklen, char **haystack, const char *needle)
{
	int i;

	for (i = 0; i < haystacklen; i++)
		if (strcasecmp(haystack[i], needle) == 0)
			return 1;

	return 0;
}

/*
 * is_textplain()
 *
 * checks if content-type is text/plain
 */
int is_textplain(struct dm_list *hdr)
{
	struct mime_record *mr;
	int i, len;

	if (!hdr)
		return 0;

	mime_findfield("content-type", hdr, &mr);

	if (!mr)
		return 0;

	len = strlen(mr->value);
	for (i = 0; len - i >= (int) sizeof("text/plain"); i++)
		if (strncasecmp
		    (&mr->value[i], "text/plain",
		     sizeof("text/plain") - 1) == 0)
			return 1;

	return 0;
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
 * base64encode()
 *
 * encodes a string using base64 encoding
 */
void base64encode(char *in, char *out)
{
	for (; strlen(in) >= 3; in += 3) {
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ =
		    base64encodestring[((in[0] & 0x03) << 4U) |
				       ((in[1] & 0xF0) >> 4U)];
		*out++ =
		    base64encodestring[((in[1] & 0x0F) << 2U) |
				       ((in[2] & 0xC0) >> 6U)];
		*out++ = base64encodestring[(in[2] & 0x3F)];
	}

	if (strlen(in) == 2) {
		/* 16 bits left to encode */
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ =
		    base64encodestring[((in[0] & 0x03) << 4U) |
				       ((in[1] & 0xF0) >> 4U)];
		*out++ = base64encodestring[((in[1] & 0x0F) << 2U)];
		*out++ = '=';

		return;
	}

	if (strlen(in) == 1) {
		/* 8 bits left to encode */
		*out++ = base64encodestring[(in[0] & 0xFC) >> 2U];
		*out++ = base64encodestring[((in[0] & 0x03) << 4U)];
		*out++ = '=';
		*out++ = '=';

		return;
	}
}


/*
 * base64decode()
 *
 * decodes a base64 encoded string
 */
void base64decode(char *in, char *out)
{
	for (; strlen(in) >= 4; in += 4) {
		*out++ = (stridx(base64encodestring, in[0]) << 2U)
		    | ((stridx(base64encodestring, in[1]) & 0x30) >> 4U);

		*out++ = ((stridx(base64encodestring, in[1]) & 0x0F) << 4U)
		    | ((stridx(base64encodestring, in[2]) & 0x3C) >> 2U);

		*out++ = ((stridx(base64encodestring, in[2]) & 0x03) << 6U)
		    | (stridx(base64encodestring, in[3]) & 0x3F);
	}

	*out = 0;
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
 * build_imap_search()
 *
 * builds a linked list of search items from a set of IMAP search keys
 * sl should be initialized; new search items are simply added to the list
 *
 * returns -1 on syntax error, -2 on memory error; 0 on success, 1 if ')' has been encountered
 */

int build_imap_search(char **search_keys, struct dm_list *sl, int *idx, int sorted)
{
	search_key_t key;
	int result;

	if (!search_keys || !search_keys[*idx])
		return 0;

	memset(&key, 0, sizeof(key));
	
	/* coming from _ic_sort */

	if(sorted && (strcasecmp(search_keys[*idx], "reverse") == 0)) {
		(*idx)++;
		key.reverse = TRUE;
	}
		
	if(sorted && (strcasecmp(search_keys[*idx], "arrival") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%sphysmessage", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "internal_date");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "from") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%sfromfield", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "fromaddr");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "subject") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%ssubjectfield", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "subjectfield");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "cc") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%sccfield", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "ccaddr");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "to") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%stofield", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "toaddr");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "size") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%sphysmessage", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "messagesize");
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "date") == 0)) {
		
		key.type = IST_SORT_FLD;
		g_snprintf(key.table, MAX_SEARCH_LEN, "%sdatefield", DBPFX);
		g_snprintf(key.field, MAX_SEARCH_LEN, "datefield");
		(*idx)++;
		
//	} else if(sorted && (strcasecmp(search_keys[*idx], "all") == 0)) {
//		/* TODO */ 
//		(*idx)++;
	
	
	} else if(sorted && (strcasecmp(search_keys[*idx], "us-ascii") == 0)) {
	
		(*idx)++;
		
	} else if(sorted && (strcasecmp(search_keys[*idx], "iso-8859-1") == 0)) {
		
		(*idx)++;
		
/* no silent failures for now */
		

	} else if (strcasecmp(search_keys[*idx], "all") == 0) {
		
		key.type = IST_SET;
		strcpy(key.search, "1:*");
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "uid") == 0) {
		
		if (!search_keys[*idx + 1])
			return -1;
		(*idx)++;

		key.type = IST_SET_UID;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		
		if (!check_msg_set(key.search))
			return -1;
		(*idx)++;
	}

	/*
	 * FLAG search keys
	 */

	else if (strcasecmp(search_keys[*idx], "answered") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "answered_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "deleted") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "deleted_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "flagged") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "flagged_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "recent") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "recent_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "seen") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "seen_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "keyword") == 0) {
		
		/* no results from this one */
		if (!search_keys[(*idx) + 1])
			return -1;
		(*idx)++;

		key.type = IST_SET;
		strcpy(key.search, "0");
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "draft") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "draft_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "new") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "(seen_flag=0 AND recent_flag=1)", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "old") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "recent_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "unanswered") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "answered_flag=0", MAX_SEARCH_LEN);
		(*idx)++;

	} else if (strcasecmp(search_keys[*idx], "undeleted") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "deleted_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unflagged") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "flagged_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unseen") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "seen_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unkeyword") == 0) {
	
		/* matches every msg */
		if (!search_keys[(*idx) + 1])
			return -1;
		(*idx)++;

		key.type = IST_SET;
		strcpy(key.search, "1:*");
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "undraft") == 0) {
		
		key.type = IST_FLAG;
		strncpy(key.search, "draft_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * HEADER search keys
	 */

	else if (strcasecmp(search_keys[*idx], "bcc") == 0) {
		
		key.type = IST_HDR;
		strncpy(key.hdrfld, "bcc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "cc") == 0) {
		
		key.type = IST_HDR;
		strncpy(key.hdrfld, "cc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "from") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "from", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "to") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "to", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "subject") == 0) {
		key.type = IST_HDR;
		strncpy(key.hdrfld, "subject", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "header") == 0) {
		
		key.type = IST_HDR;
		if (!search_keys[(*idx) + 1] || !search_keys[(*idx) + 2])
			return -1;

		strncpy(key.hdrfld, search_keys[(*idx) + 1], MIME_FIELD_MAX);
		strncpy(key.search, search_keys[(*idx) + 2], MAX_SEARCH_LEN);

		(*idx) += 3;

	} else if (strcasecmp(search_keys[*idx], "sentbefore") == 0) {
	
		key.type = IST_HDRDATE_BEFORE;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;

	} else if (strcasecmp(search_keys[*idx], "senton") == 0) {
		
		key.type = IST_HDRDATE_ON;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "sentsince") == 0) {
		
		key.type = IST_HDRDATE_SINCE;
		strncpy(key.hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * INTERNALDATE keys
	 */

	else if (strcasecmp(search_keys[*idx], "before") == 0) {
		
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;
		
		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;
		
		g_snprintf(key.search, MAX_SEARCH_LEN, "internal_date < '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "on") == 0) {
		
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		g_snprintf(key.search, MAX_SEARCH_LEN, "internal_date LIKE '%s%%'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "since") == 0) {
		
		key.type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		g_snprintf(key.search, MAX_SEARCH_LEN, "internal_date > '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
	}

	/*
	 * DATA-keys
	 */

	else if (strcasecmp(search_keys[*idx], "body") == 0) {
		
		key.type = IST_DATA_BODY;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "text") == 0) {
	
		key.type = IST_DATA_TEXT;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(key.search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * SIZE keys
	 */

	else if (strcasecmp(search_keys[*idx], "larger") == 0) {
		
		key.type = IST_SIZE_LARGER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		key.size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "smaller") == 0) {
	
		key.type = IST_SIZE_SMALLER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		key.size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	}

	/*
	 * NOT, OR, ()
	 */
	
	else if (strcasecmp(search_keys[*idx], "not") == 0) {
	
		key.type = IST_SUBSEARCH_NOT;

		(*idx)++;
		
		if ((result = build_imap_search(search_keys, &key.sub_search, idx, sorted )) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		/* a NOT should be unary */
		if (key.sub_search.total_nodes != 1) {
			free_searchlist(&key.sub_search);
			return -1;
		}
		
	} else if (strcasecmp(search_keys[*idx], "or") == 0) {
		
		key.type = IST_SUBSEARCH_OR;

		(*idx)++;
		if ((result = build_imap_search(search_keys, &key.sub_search, idx, sorted)) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		if ((result = build_imap_search(search_keys, &key.sub_search, idx, sorted )) < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		/* an OR should be binary */
		if (key.sub_search.total_nodes != 2) {
			free_searchlist(&key.sub_search);
			return -1;
		}

	} else if (strcasecmp(search_keys[*idx], "(") == 0) {
		
		key.type = IST_SUBSEARCH_AND;

		(*idx)++;
		while ((result = build_imap_search(search_keys, &key.sub_search, idx, sorted)) == 0 && search_keys[*idx]);

		if (result < 0) {
			dm_list_free(&key.sub_search.start);
			return result;
		}

		if (result == 0) {
			/* 
			 * no ')' encountered (should not happen, 
			 * parentheses are matched at the command 
			 * line) 
			 */
			free_searchlist(&key.sub_search);
			return -1;
		}
	} else if (strcasecmp(search_keys[*idx], ")") == 0) {
		
		(*idx)++;
		return 1;
	
	} else if (check_msg_set(search_keys[*idx])) {
	
		key.type = IST_SET;
		strncpy(key.search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else {
		/* unknown search key */
		return -1;
	}

	if (!dm_list_nodeadd(sl, &key, sizeof(key)))
		return -2;

	return 0;
}


/*
 * perform_imap_search()
 *
 * returns 0 on succes, -1 on dbase error, -2 on memory error, 1 if result set is too small
 * (new mail has been added to mailbox while searching, mailbox data out of sync)
 */
int perform_imap_search(unsigned int *rset, int setlen, search_key_t * sk, mailbox_t * mb, int sorted, int condition)
{
	search_key_t *subsk;
	struct element *el;
	int result, i;
	unsigned int *newset = NULL;
	int subtype = IST_SUBSEARCH_OR;


	if (!setlen) // empty mailbox
		return 0;
	
	if (!rset) {
		trace(TRACE_ERROR,"%s,%s: error empty rset", __FILE__, __func__);
		return -2;	/* stupidity */
	}

	if (!sk) {
		trace(TRACE_ERROR,"%s,%s: error empty sk", __FILE__, __func__);
		return -2;	/* no search */
	}


	trace(TRACE_DEBUG,"%s,%s: search_key [%d] condition [%d]", __FILE__, __func__, sk->type, condition);
	
	switch (sk->type) {
	case IST_SET:
		build_set(rset, setlen, sk->search);
		break;

	case IST_SET_UID:
		build_uid_set(rset, setlen, sk->search, mb);
		break;

	case IST_SORT:
	case IST_SORT_FLD:
		result = db_search(rset, setlen, sk, mb);
		return result;
		break;

	case IST_SORTHDR:
		result = db_sort_parsed(rset, setlen, sk, mb, condition);
		return 0;
		break;

	case IST_HDRDATE_BEFORE:
	case IST_HDRDATE_SINCE:
	case IST_HDRDATE_ON:
	case IST_IDATE:
	case IST_FLAG:
	case IST_HDR:
		if ((result = db_search(rset, setlen, sk, mb)))
			return result;
		break;
	/* 
	 * these all have in common that all messages need to be parsed 
	 */
	case IST_DATA_BODY:
	case IST_DATA_TEXT:
	case IST_SIZE_LARGER:
	case IST_SIZE_SMALLER:
		result = db_search_parsed(rset, setlen, sk, mb, condition);
		break;

	case IST_SUBSEARCH_NOT:
	case IST_SUBSEARCH_AND:
		subtype = IST_SUBSEARCH_AND;

	case IST_SUBSEARCH_OR:

		if (! (newset = (unsigned int *)g_malloc0(sizeof(int) * setlen)))
			return -2;
		
		el = dm_list_getstart(&sk->sub_search);
		while (el) {
			subsk = (search_key_t *) el->data;

			if (subsk->type == IST_SUBSEARCH_OR)
				memset(newset, 0, sizeof(int) * setlen);
			else
				for (i = 0; i < setlen; i++)
					newset[i] = rset[i];

			if ((result = perform_imap_search(newset, setlen, subsk, mb, sorted, sk->type))) {
				dm_free(newset);
				return result;
			}

			if (! sorted)
				combine_sets(rset, newset, setlen, subtype);
			else {
				for (i = 0; i < setlen; i++)
					rset[i] = newset[i];
			}
 
			el = el->nextnode;
		}

		if (sk->type == IST_SUBSEARCH_NOT)
			invert_set(rset, setlen);

		break;

	default:
		dm_free(newset);
		return -2;	/* ??? */
	}

	dm_free(newset);
	return 0;
}


/*
 * frees the search-list sl
 *
 */
void free_searchlist(struct dm_list *sl)
{
	search_key_t *sk;
	struct element *el;

	if (!sl)
		return;

	el = dm_list_getstart(sl);

	while (el) {
		sk = (search_key_t *) el->data;

		free_searchlist(&sk->sub_search);
		dm_list_free(&sk->sub_search.start);

		el = el->nextnode;
	}

	dm_list_free(&sl->start);
	return;
}


void invert_set(unsigned int *set, int setlen)
{
	int i;

	if (!set)
		return;

	for (i = 0; i < setlen; i++)
		set[i] = !set[i];
}


void combine_sets(unsigned int *dest, unsigned int *sec, int setlen, int type)
{
	int i;

	if (!dest || !sec)
		return;

	if (type == IST_SUBSEARCH_AND) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] && dest[i]);
	} else if (type == IST_SUBSEARCH_OR) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] || dest[i]);
	}
}


/* 
 * build_set()
 *
 * builds a msn-set from a IMAP message set spec. the IMAP set is supposed to be correct,
 * no checks are performed.
 */
void build_set(unsigned int *set, unsigned int setlen, char *cset)
{
	unsigned int i;
	u64_t num, num2;
	char *sep = NULL;

	if ((! set) || (! cset))
		return;

	memset(set, 0, setlen * sizeof(int));

	do {
		num = strtoull(cset, &sep, 10);
		if (num <= setlen && num > 0) {
			if (!*sep)
				set[num - 1] = 1;
			else if (*sep == ',') {
				set[num - 1] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = num - 1; i < setlen; i++)
						set[i] = 1;
					cset = sep + 1;
				} else {
					cset = sep;
					num2 = strtoull(cset, &sep, 10);

					if (num2 > setlen)
						num2 = setlen;
					if (num2 > 0) {
						/* NOTE: here: num2 > 0, num > 0 */
						if (num2 < num) {
							/* swap! */
							i = num;
							num = num2;
							num2 = i;
						}

						for (i = num - 1; i < num2; i++)
							set[i] = 1;
					}
					if (*sep)
						cset = sep + 1;
				}
			}
		} else if (*sep) {
			/* invalid char, skip it */
			cset = sep + 1;
			sep++;
		}
	} while (sep && *sep && cset && *cset);
}


/* 
 * build_uid_set()
 *
 * as build_set() but takes uid's instead of MSN's
 */
void build_uid_set(unsigned int *set, unsigned int setlen, char *cset,
		   mailbox_t * mb)
{
	unsigned int i, msn, msn2;
	int result;
	int num2found = 0;
	u64_t num, num2;
	char *sep = NULL;

	if (!set)
		return;

	memset(set, 0, setlen * sizeof(int));

	if (!cset || setlen == 0)
		return;

	do {
		num = strtoull(cset, &sep, 10);
		result =
		    binary_search(mb->seq_list, mb->exists, num, &msn);

		if (result < 0 && num < mb->seq_list[mb->exists - 1]) {
			/* ok this num is not a UID, but if a range is specified (i.e. 1:*) 
			 * it is valid -> check *sep
			 */
			if (*sep == ':') {
				result = 1;
				for (msn = 0; mb->seq_list[msn] < num;
				     msn++);
				if (msn >= mb->exists)
					msn = mb->exists - 1;
			}
		}

		if (result >= 0) {
			if (!*sep)
				set[msn] = 1;
			else if (*sep == ',') {
				set[msn] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = msn; i < setlen; i++)
						set[i] = 1;

					cset = sep + 1;
				} else {
					/* fetch second number */
					cset = sep;
					num2 = strtoull(cset, &sep, 10);
					result =
					    binary_search(mb->seq_list,
							  mb->exists, num2,
							  &msn2);

					if (result < 0) {
						/* in a range: (like 1:1000) so this number doesnt need to exist;
						 * find the closest match below this UID value
						 */
						if (mb->exists == 0)
							num2found = 0;
						else {
							for (msn2 =
							     mb->exists -
							     1;; msn2--) {
								if (msn2 ==
								    0
								    && mb->
								    seq_list
								    [msn2]
								    > num2) {
									num2found
									    =
									    0;
									break;
								} else
								    if
								    (mb->
								     seq_list
								     [msn2]
								     <=
								     num2)
								{
									/* found! */
									num2found
									    =
									    1;
									break;
								}
							}
						}

					} else
						num2found = 1;

					if (num2found == 1) {
						if (msn2 < msn) {
							/* swap! */
							i = msn;
							msn = msn2;
							msn2 = i;
						}

						for (i = msn; i <= msn2;
						     i++)
							set[i] = 1;
					}

					if (*sep)
						cset = sep + 1;
				}
			}
		} else {
			/* invalid num, skip it */
			if (*sep) {
				cset = sep + 1;
				sep++;
			}
		}
	} while (sep && *sep && cset && *cset);
}


void dumpsearch(search_key_t * sk, int level)
{
	char *spaces = (char *) dm_malloc(level * 3 + 1);
	struct element *el;
	search_key_t *subsk;

	if (!spaces)
		return;

	memset(spaces, ' ', level * 3);
	spaces[level * 3] = 0;

	if (!sk) {
		trace(TRACE_DEBUG, "%s(null)\n", spaces);
		dm_free(spaces);
		return;
	}

	switch (sk->type) {
	case IST_SUBSEARCH_NOT:
		trace(TRACE_DEBUG, "%sNOT\n", spaces);

		el = dm_list_getstart(&sk->sub_search);
		if (el)
			subsk = (search_key_t *) el->data;
		else
			subsk = NULL;

		dumpsearch(subsk, level + 1);
		break;

	case IST_SUBSEARCH_AND:
		trace(TRACE_DEBUG, "%sAND\n", spaces);
		el = dm_list_getstart(&sk->sub_search);

		while (el) {
			subsk = (search_key_t *) el->data;
			dumpsearch(subsk, level + 1);

			el = el->nextnode;
		}
		break;

	case IST_SUBSEARCH_OR:
		trace(TRACE_DEBUG, "%sOR\n", spaces);
		el = dm_list_getstart(&sk->sub_search);

		while (el) {
			subsk = (search_key_t *) el->data;
			dumpsearch(subsk, level + 1);

			el = el->nextnode;
		}
		break;

	default:
		trace(TRACE_DEBUG, "%s[type %d] \"%s\"\n", spaces,
		      sk->type, sk->search);
	}

	dm_free(spaces);
	return;
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

/* 
 * init cache 
 */
int init_cache()
{
	cached_msg.dmsg = NULL;
	cached_msg.num = -1;
	cached_msg.msg_parsed = 0;
	cached_msg.memdump = mopen();
	if (!cached_msg.memdump)
		return -1;

	cached_msg.tmpdump = mopen();
	if (!cached_msg.tmpdump) {
		mclose(&cached_msg.memdump);
		return -1;
	}

	cached_msg.file_dumped = 0;
	cached_msg.dumpsize = 0;
	return 0;
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




