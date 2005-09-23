/*
 *  Copyright (C) 2004  Paul Stevens <paul@nfg.nl>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  $Id: check_dbmail_imapd.c 1874 2005-08-29 15:13:06Z paul $ 
 *
 *
 *  
 *
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <stdlib.h>
#include <check.h>
#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

#include "dbmail-imapsession.h"
#include "dbmail-message.h"
#include "mime.h"
#include "rfcmsg.h"
#include "dbmsgbuf.h"
#include "imaputil.h"
#include "misc.h"
#include "debug.h"
#include "db.h"
#include "auth.h"


#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
extern char *msgbuf_buf;
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

extern char *multipart_message;
extern char *multipart_message_part;
extern char *raw_lmtp_data;

void print_mimelist(struct dm_list *mimelist)
{
	struct element *el;
	struct mime_record *mr;
	el = dm_list_getstart(mimelist);
	while (el) {
		mr = el->data;
		printf("field [%s], value [%s]\n", mr->field, mr->value);
		el = el->nextnode;
	}
}
/*
 *
 * the test fixtures
 *
 */
	
void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}
	
void setup(void)
{
	configure_debug(5,1,0);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
	g_mime_init(0);
	init_testuser1();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
}


START_TEST(test_g_list_join)
{
	GString *result;
	GList *l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL NIL (NIL NIL) (NIL NIL)")==0,"g_list_join failed");
	g_string_free(result,TRUE);

	l = NULL;
	l = g_list_append(l, "NIL");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL")==0,"g_list_join failed");
	g_string_free(result,TRUE);

}
END_TEST

START_TEST(test_dbmail_imap_plist_as_string)
{
	char *result;
	GList *l;

	l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
	
	l = NULL;
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);

	l = g_list_append(NULL, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL (NIL NIL) (NIL NIL))")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
}
END_TEST


#define A(x,y) fail_unless(strcmp(y,dbmail_imap_astring_as_string(x))==0,"dbmail_imap_astring_as_string failed")
START_TEST(test_dbmail_imap_astring_as_string)
{
	A("test","\"test\"");
	A("\"test\"","\"test\"");
	A("\"test","{5}\r\n\"test");
	A("testÃ","{5}\r\ntestÃ");
	A("test\"","{5}\r\ntest\"");
	A("test\"","{5}\r\ntest\"");
	A("test\\","{5}\r\ntest\\");
	A("test\225","{5}\r\ntest\225");
}
END_TEST

START_TEST(test_imap_session_new)
{
	struct ImapSession *s;
	s = dbmail_imap_session_new();
	fail_unless(s!=NULL, "Failed to initialize imapsession");
	dbmail_imap_session_delete(s);
}
END_TEST

START_TEST(test_imap_bodyfetch)
{
	int result;
	guint64 octet;
	struct ImapSession *s = dbmail_imap_session_new();
	dbmail_imap_session_bodyfetch_new(s);
	
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetstart(s), "octetstart init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetcnt(s), "octetcnt init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_argstart(s), "argstart init value incorrect");
	
	dbmail_imap_session_bodyfetch_set_argstart(s,23);
	result = dbmail_imap_session_bodyfetch_get_last_argstart(s);
	fail_unless(result==23, "argstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetstart(s,0);
	octet = dbmail_imap_session_bodyfetch_get_last_octetstart(s);
	fail_unless(octet==0, "octetstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetcnt(s,12288);
	octet = dbmail_imap_session_bodyfetch_get_last_octetcnt(s);
	fail_unless(octet==12288, "octetcnt incorrect");
	
	dbmail_imap_session_delete(s);
		
}
END_TEST

START_TEST(test_mime_readheader)
{
	int res;
	u64_t blkidx=0, headersize=0;
	struct dm_list mimelist;
	struct DbmailMessage *m, *p;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	dm_list_init(&mimelist);
	res = mime_readheader(m,&blkidx,&mimelist,&headersize);
	fail_unless(res==10, "number of headers incorrect");
	fail_unless(blkidx==485, "blkidx incorrect");
	fail_unless(headersize==blkidx, "headersize incorrect");
	fail_unless(dm_list_length(&mimelist)==10, "number of message-headers incorrect");
	dm_list_free(&mimelist.start);
	
	blkidx = 0; headersize = 0;

	p = dbmail_message_new();
	p = dbmail_message_init_with_string(p,g_string_new(multipart_message_part));
	
	dm_list_init(&mimelist);
	res = mime_readheader(p, &blkidx, &mimelist, &headersize);
	fail_unless(res==3, "number of headers incorrect");
	fail_unless(blkidx==142, "blkidx incorrect");
	fail_unless(headersize==blkidx, "headersize incorrect");
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	dm_list_free(&mimelist.start);

	dbmail_message_free(m);
	dbmail_message_free(p);
	
}
END_TEST

START_TEST(test_mime_fetch_headers)
{
	struct dm_list mimelist;
	struct mime_record *mr;
	struct DbmailMessage *m, *p;

	
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	dm_list_init(&mimelist);
	mime_fetch_headers(m,&mimelist);
	fail_unless(mimelist.total_nodes==10, "number of message-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Content-Type")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "multipart/mixed; boundary=boundary")==0, "Field value incorrect");
	
	dm_list_free(&mimelist.start);

	p = dbmail_message_new();
	p = dbmail_message_init_with_string(p,g_string_new(multipart_message_part));
	
	dm_list_init(&mimelist);
	mime_fetch_headers(p,&mimelist);
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Content-Disposition")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "inline; filename=\"mime_alternative\"")==0, "Field value incorrect");
	
	dm_list_free(&mimelist.start);

	dbmail_message_free(m);
	dbmail_message_free(p);

}
END_TEST

//int mail_address_build_list(char *scan_for_field, struct dm_list *targetlist,
//	                  struct dm_list *mimelist)
START_TEST(test_mail_address_build_list)
{
	int result;
	struct dm_list targetlist;
	struct dm_list mimelist;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	dm_list_init(&targetlist);
	dm_list_init(&mimelist);
	
	mime_fetch_headers(m, &mimelist);

	result = mail_address_build_list("Cc", &targetlist, &mimelist);
	struct element *el;
	el = targetlist.start;

	fail_unless(result==0, "mail_address_build_list failed");
	fail_unless(targetlist.total_nodes==2,"mail_address_build_list failed");
	fail_unless(strcmp((char *)el->data,"nobody@test123.com")==0, "mail_address_build_list failed");

	dbmail_message_free(m);
}
END_TEST

//int db_fetch_headers(u64_t msguid, mime_message_t * msg)
START_TEST(test_db_fetch_headers)
{
	u64_t physid;
	u64_t user_idnr;
	int res;
	mime_message_t *message;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(multipart_message));
	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);

	physid = dbmail_message_get_physid(m);
	auth_user_exists("testuser1",&user_idnr);
	fail_unless(user_idnr > 0, "db_fetch_headers failed. Try adding [testuser1]");

	sort_and_deliver(m,user_idnr,"INBOX");
	
	message = db_new_msg();
	res = db_fetch_headers(m->id, message);
	fail_unless(res==0,"db_fetch_headers failed");
	
	db_free_msg(message);
	dbmail_message_free(m);
}
END_TEST

static void handle_part(GMimeObject *part, gpointer data, gboolean extension);
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
	if (hash) 
		g_hash_table_foreach(hash, get_param_list, (gpointer)&(l));
	if (l) {
		list = g_list_append_printf(list, "%s", dbmail_imap_plist_as_string(l));
		g_list_foreach(l,(GFunc)g_free,NULL);
		g_list_free(l);
	}
	return list;
}
static GList * imap_append_disposition_as_string(GList *list, GMimeObject *part)
{
	GList *t = NULL;
	const GMimeDisposition *disposition;
	char *result;
	const char *disp = g_mime_object_get_header(part, "Content-Disposition");
	
	if(disp) {
		disposition = g_mime_disposition_new(disp);
		t = g_list_append_printf(t,"\"%s\"",disposition->disposition);
		
		/* paramlist */
		t = imap_append_hash_as_string(t, disposition->param_hash);
		result = dbmail_imap_plist_as_string(t);
		
		g_list_foreach(t,(GFunc)g_free,NULL);
		g_list_free(t);
		
		list = g_list_append_printf(list,"%s",result);

		g_free(result);
	} else {
		list = g_list_append_printf(list,"%s","NIL");
	}
	return list;
}
static GList * imap_append_header_as_string(GList *list, GMimeObject *part, const char *header)
{
	char *result;
	if((result = (char *)g_mime_object_get_header(part,header))) {
		list = g_list_append_printf(list,"\"%s\"",result);
		g_free(result);
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
	
	b = g_string_erase(b,0,strlen(h));
	t = get_crlf_encoded(b->str);
	s = strlen(t);
	
	/* count body lines */
	v = t;
	i = 0;
	while (v[i++]) {
		if (v[i]=='\n')
			l++;
	}
	if (v[s-1] != '\n')
		l++;
	
	
	g_free(h);
	g_free(t);
	g_string_free(b,TRUE);
	*size = s;
	*lines = l;
}


void handle_part(GMimeObject *part, gpointer data, gboolean extension)
{
	const GMimeContentType *type = g_mime_object_get_content_type(part);
	if (! type)
		return;

	/* simple message */
	if (g_mime_content_type_is_type(type,"text","*"))
		_structure_part_text(part,data, extension);
	/* multipart composite */
	else if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(part,data, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(part,data, extension);

}

void _structure_part_multipart(GMimeObject *part, gpointer data, gboolean extension)
{
	GMimeMultipart *multipart;
	GMimeObject *subpart;
	GList *list = NULL;
	GString *s;
	int i,j;
	
	const GMimeContentType *type = g_mime_object_get_content_type(part);

	multipart = GMIME_MULTIPART(part);
	i = g_mime_multipart_get_number(multipart);
	
	/* loop over parts for base info */
	for (j=0; j<i; j++) {
		subpart = g_mime_multipart_get_part(multipart,j);
		handle_part(subpart,data,extension);
	}
	
	/* sub-type */
	*(GList **)data = g_list_append_printf(*(GList **)data,"\"%s\"", type->subtype);

	/* extension data (only for multipart, in case of BODYSTRUCTURE command argument) */
	if (extension) {
		list = imap_append_disposition_as_string(list, part);
		list = imap_append_header_as_string(list,part,"Content-Language");
		list = imap_append_header_as_string(list,part,"Content-Location");
		s = g_list_join(list," ");
		
		*(GList **)data = (gpointer)g_list_append(*(GList **)data,s->str);

		g_list_foreach(list,(GFunc)g_free,NULL);
		g_list_free(list);
		g_string_free(s,FALSE);
	}
	
}
void _structure_part_message_rfc822(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result;
	GList *list = NULL;
	size_t s, l=0;
	
	const GMimeContentType *type = g_mime_object_get_content_type(part);
	if (! type)
		return;

	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, type->param_hash);
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(part)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,part,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string(list,part,"Content-Transfer-Encoding");
	/* body size */
	imap_part_get_sizes(part,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);

	/* envelope structure */

	/* body structure */

	/* lines */
	list = g_list_append_printf(list,"%d", l);
}
void _structure_part_text(GMimeObject *part, gpointer data, gboolean extension)
{
	char *result;
	GList *list = NULL;
	size_t s, l=0;
	
	const GMimeContentType *type = g_mime_object_get_content_type(part);
	if (! type)
		return;

	/* type/subtype */
	list = g_list_append_printf(list,"\"%s\"", type->type);
	list = g_list_append_printf(list,"\"%s\"", type->subtype);
	/* paramlist */
	list = imap_append_hash_as_string(list, type->param_hash);
	/* body id */
	if ((result = (char *)g_mime_object_get_content_id(part)))
		list = g_list_append_printf(list,"\"%s\"", result);
	else
		list = g_list_append_printf(list,"NIL");
	/* body description */
	list = imap_append_header_as_string(list,part,"Content-Description");
	/* body encoding */
	list = imap_append_header_as_string(list,part,"Content-Transfer-Encoding");
	/* body size */
	imap_part_get_sizes(part,&s,&l);
	
	list = g_list_append_printf(list,"%d", s);
	/* body lines */
	list = g_list_append_printf(list,"%d", l);
	/* extension data in case of BODYSTRUCTURE */
	
	if (extension) {
		/* body md5 */
		list = imap_append_header_as_string(list,part,"Content-MD5");
		/* body disposition */
		list = imap_append_disposition_as_string(list,part);
		/* body language */
		list = imap_append_header_as_string(list,part,"Content-Language");
		/* body location */
		list = imap_append_header_as_string(list,part,"Content-Location");
	}
	
	/* done*/
	*(GList **)data = (gpointer)g_list_append(*(GList **)data,dbmail_imap_plist_as_string(list));
	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_free(list);
}

GList * imap_get_structure(GMimeMessage *message, gboolean extension) {
	GList *structure = NULL;
	GMimeContentType *type;
	GMimeObject *part;
	
	part = g_mime_message_get_mime_part(message);
	type = (GMimeContentType *)g_mime_object_get_content_type(part);
	if (! type)
		return NULL;

	/* simple message */
	if (g_mime_content_type_is_type(type,"text","*"))
		_structure_part_text(part,(gpointer)&structure, extension);
	/* multipart composite */
	else if (g_mime_content_type_is_type(type,"multipart","*"))
		_structure_part_multipart(part,(gpointer)&structure, extension);
	/* message included as mimepart */
	else if (g_mime_content_type_is_type(type,"message","rfc822"))
		_structure_part_message_rfc822(part,(gpointer)&structure, extension);
	
	return structure;
}


START_TEST(test_imap_get_structure)
{
	struct DbmailMessage *message;
	char *result;
	GList *l;

	/* multipart */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_message));
	l = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	result = dbmail_imap_plist_as_string(l);
	
	g_list_foreach(l,(GFunc)g_free,NULL);

	printf("\n[%s]\n[%s]\n","(\"text\" \"plain\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 36 3 NIL NIL NIL) \"mixed\" (\"boundary\" \"===============1374485421==\") NIL NIL)",result);
	g_free(result);
	dbmail_message_free(message);

	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	l = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcasecmp(result,"(\"text\" \"plain\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 34 4 NIL NIL NIL NIL)")==0,
		"get_structure failed");
	g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
	dbmail_message_free(message);

}
END_TEST

GList * imap_get_envelope(GMimeMessage *message)
{
	GMimeObject *part = GMIME_OBJECT(message);
	InternetAddressList *alist;
	GList *list = NULL;
	char *result;
	
	/* date */
	result = g_mime_message_get_date_string(message);
	if (result)
		list = g_list_append_printf(list,"%s", result);
	else
		list = g_list_append_printf(list,"%s", "NIL");
	
	/* subject */
	result = g_mime_message_get_subject(message);
	if (result)
		list = g_list_append_printf(list,"%s", result);
	else
		list = g_list_append_printf(list,"%s", "NIL");
	
	/* from */

	/* sender */

	/* reply-to */

	/* to */
	alist = g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_TO);
	
	/* cc */
	alist = g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_CC);
	
	/* bcc */
	alist = g_mime_message_get_recipients(message,GMIME_RECIPIENT_TYPE_BCC);

	/* in-reply-to */
	list = imap_append_header_as_string(list,part,"In-Reply-to");
	/* message-id */
	result = g_mime_message_get_message_id(message);
	if (result)
		list = g_list_append_printf(list,"%s", result);
	else
		list = g_list_append_printf(list,"%s", "NIL");

	return list;
}

START_TEST(test_imap_get_envelope)
{
	struct DbmailMessage *message;
	char *result;
	GList *l = NULL;
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	l = imap_get_envelope(message->content);

}
END_TEST

START_TEST(test_g_list_slices)
{
	unsigned i=0;
	unsigned j=98;
	unsigned s=11;
	GList *list = NULL;
	GList *sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==9, "number of slices incorrect");
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==s,"Slice length incorrect");

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sub,(GFunc)g_free,NULL);
	
	i=0;
	j=17;
	s=100;
	list = NULL;
	sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	fail_unless(g_list_length(list)==1, "number of slices incorrect");
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==j,"Slice length incorrect");

}
END_TEST

unsigned int get_bound_lo(unsigned int * set, unsigned int setlen) {
	unsigned int index = 0;
	while (set[index]==0 && index < setlen) 
		index++;
	return index;
}
unsigned int get_bound_hi(unsigned int * set, unsigned int setlen) {
	int index = setlen;
	while (set[index]==0 && index >= 0) 
		index--;
	return (unsigned)index;
}
unsigned int get_count_on(unsigned int * set, unsigned int setlen) {
	unsigned int i, count = 0;
	for (i=0; i<setlen; i++)
		if (set[i])
			count++;
	return count;
}

	
	
START_TEST(test_build_set)
{
	//void build_set(unsigned int *set, unsigned int setlen, char *cset)
	unsigned int i;
	unsigned int setlen=50;
	unsigned *set = g_new0(unsigned, setlen);
	for (i=0; i<setlen; i++) {
		set[i]=1;
	}
	build_set(set, setlen, "1:*");
	fail_unless(0==get_bound_lo(set, setlen), "lower index incorrect");
	fail_unless((setlen-1)==get_bound_hi(set, setlen), "upper index incorrect");
	fail_unless(get_count_on(set,setlen)==setlen, "count incorrect");
	
	build_set(set, setlen, "1,5:*");
	fail_unless(0==get_bound_lo(set, setlen), "lower index incorrect");
	fail_unless((setlen-1)==get_bound_hi(set, setlen), "upper index incorrect");
	fail_unless(get_count_on(set,setlen)==setlen-3, "count incorrect");
}
END_TEST

/*
START_TEST(test_build_uid_set)
{
	
}
END_TEST
*/

/*
START_TEST(test_build_args_array_ext) 
{

}
END_TEST
*/

static char *wrap_base_subject(char *in) 
{
	gchar *out = g_strdup(in);
	dm_base_subject(out);
	return out;
}	

START_TEST(test_dm_base_subject)
{
	gchar *subject;

	fail_unless(strcmp(wrap_base_subject("Re: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Fwd: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Fw: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("[issue123] foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re [issue123]: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re: [issue123] foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re: [issue123] [Fwd: foo]"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("[Dbmail-dev] [DBMail 0000240]: some bug report"),"some bug report")==0,"dm_base_subject failed");

	fail_unless(strcmp(wrap_base_subject("test\t\tspaces  here"),"test spaces here")==0,"cleanup of spaces failed");
	fail_unless(strcmp(wrap_base_subject("test strip trailer here (fwd) (fwd)"),"test strip trailer here")==0,"cleanup of sub-trailer failed");
	fail_unless(strcmp(wrap_base_subject("Re: Fwd: [fwd: some silly test subject] (fwd)"),"some silly test subject")==0, "stripping of subj-tlr/subj-ldr failed");
	
	subject = g_strdup("=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=");
	dm_base_subject(subject);

}
END_TEST

#define Y(z,q) fail_unless(z==(q), "listex_match failed")
#define X(z,a,b) Y(z,listex_match(a,b,".",0))
#define N(z,a,b) Y(z,listex_match(a,b,"¿",0))
START_TEST(test_listex_match)
{
	X(1, "INBOX", "INBOX");
	X(0, "INBOX", "INBOX.Foo");
	X(0, "INBOX", "INBOX.Foo.Bar");

	X(0, "INBOX.%", "INBOX");
	X(1, "INBOX.%", "INBOX.Foo");
	X(0, "INBOX.%", "INBOX.Foo.Bar");

	X(0, "INBOX.*", "INBOX");
	X(1, "INBOX.*", "INBOX.Foo");
	X(1, "INBOX.*", "INBOX.Foo.Bar");

	X(1, "INBOX*", "INBOX");
	X(1, "INBOX*", "INBOX.Foo");
	X(1, "INBOX*", "INBOX.Foo.Bar");

	X(1, "INBOX%", "INBOX");
	X(0, "INBOX%", "INBOX.Foo");
	X(0, "INBOX%", "INBOX.Foo.Bar");

	X(0, "INBOX*Foo", "INBOX");
	X(1, "INBOX*Foo", "INBOX.Foo");
	X(0, "INBOX*Foo", "INBOX.Foo.Bar");

	X(0, "INBOX*Bar", "INBOX");
	X(0, "INBOX*Bar", "INBOX.Foo");
	X(1, "INBOX*Bar", "INBOX.Foo.Bar");

	X(0, "INBOX.*Bar", "INBOX");
	X(0, "INBOX.*Bar", "INBOX.Foo");
	X(1, "INBOX.*Bar", "INBOX.Foo.Bar");

	N(0, "INBOX\317\200*Bar", "INBOX");
	N(0, "INBOX\317\200*Bar", "INBOX\317\200""Foo");
	N(1, "INBOX\317\200*Bar", "INBOX\317\200""Foo\317\200""Bar");

}
END_TEST

START_TEST(test_dm_getguid)
{
	fail_unless(dm_getguid(1) < dm_getguid(99), "dm_getguid failed");
	fail_unless(dm_getguid(99) < dm_getguid(1), "dm_getguid failed");
}
END_TEST

/* this test will fail if you're not in the CET timezone */
#define D(x,y) fail_unless(strncasecmp(date_sql2imap(x),y,IMAP_INTERNALDATE_LEN)==0,"date_sql2imap failed")
START_TEST(test_date_sql2imap)
{
        D("2005-05-03 14:10:06","Tue, 03 May 2005 14:10:06 +0200");
        D("2005-01-03 14:10:06","Mon, 03 Jan 2005 14:10:06 +0100");
}
END_TEST

Suite *dbmail_suite(void)
{
	Suite *s = suite_create("Dbmail Imap");
	TCase *tc_session = tcase_create("ImapSession");
	TCase *tc_rfcmsg = tcase_create("Rfcmsg");
	TCase *tc_mime = tcase_create("Mime");
	TCase *tc_util = tcase_create("Utils");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_session);
	suite_add_tcase(s, tc_rfcmsg);
	suite_add_tcase(s, tc_mime);
	suite_add_tcase(s, tc_util);
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_session, setup, teardown);
	tcase_add_test(tc_session, test_imap_session_new);
	tcase_add_test(tc_session, test_imap_bodyfetch);
	tcase_add_test(tc_session, test_imap_get_structure);
	tcase_add_test(tc_session, test_imap_get_envelope);
	
	tcase_add_checked_fixture(tc_rfcmsg, setup, teardown);
	tcase_add_test(tc_rfcmsg, test_db_fetch_headers);

	tcase_add_checked_fixture(tc_mime, setup, teardown);
	tcase_add_test(tc_mime, test_mime_readheader);
	tcase_add_test(tc_mime, test_mime_fetch_headers);
	tcase_add_test(tc_mime, test_mail_address_build_list);

	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_g_list_join);
	tcase_add_test(tc_util, test_dbmail_imap_plist_as_string);
	tcase_add_test(tc_util, test_dbmail_imap_astring_as_string);
	tcase_add_test(tc_util, test_g_list_slices);
	tcase_add_test(tc_util, test_build_set);
	tcase_add_test(tc_util, test_listex_match);
	tcase_add_test(tc_util, test_date_sql2imap);

	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_base_subject);
	tcase_add_test(tc_misc, test_dm_getguid);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

