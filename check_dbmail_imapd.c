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
 *  $Id$ 
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

#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
extern char *msgbuf_buf;
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

extern char *raw_message;
extern char *raw_message_part;
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

void setup(void)
{
	configure_debug(5,1,0);
	g_mime_init(0);
}

void teardown(void)
{
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

}
END_TEST

START_TEST(test_dbmail_imap_plist_as_string)
{
	char *result;
	GList *l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL (NIL NIL) (NIL NIL))")==0,"plist construction failed");
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
	
	dm_list_init(&mimelist);
	res = mime_readheader(raw_message,&blkidx,&mimelist,&headersize);
	fail_unless(res==13, "number of newlines incorrect");
	fail_unless(blkidx==485, "blkidx incorrect");
	fail_unless(headersize==blkidx+res, "headersize incorrect");
	fail_unless(mimelist.total_nodes==10, "number of message-headers incorrect");
	dm_list_free(&mimelist.start);
	
	blkidx = 0; headersize = 0;

	dm_list_init(&mimelist);
	res = mime_readheader(raw_message_part, &blkidx, &mimelist, &headersize);
	fail_unless(res==6, "number of newlines incorrect");
	fail_unless(blkidx==142, "blkidx incorrect");
	fail_unless(headersize==blkidx+res, "headersize incorrect");
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	dm_list_free(&mimelist.start);
}
END_TEST

START_TEST(test_mime_fetch_headers)
{
	struct dm_list mimelist;
	struct mime_record *mr;
	
	dm_list_init(&mimelist);
	mime_fetch_headers(raw_message,&mimelist);
	fail_unless(mimelist.total_nodes==10, "number of message-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Content-Type")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "multipart/mixed; boundary=boundary")==0, "Field value incorrect");
	
	dm_list_free(&mimelist.start);

	dm_list_init(&mimelist);
	mime_fetch_headers(raw_message_part,&mimelist);
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Content-Disposition")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "inline; filename=\"mime_alternative\"")==0, "Field value incorrect");
	
	dm_list_free(&mimelist.start);


}
END_TEST

//int mail_address_build_list(char *scan_for_field, struct dm_list *targetlist,
//	                  struct dm_list *mimelist)
START_TEST(test_mail_address_build_list)
{
	int result;
	struct dm_list targetlist;
	struct dm_list mimelist;

	dm_list_init(&targetlist);
	dm_list_init(&mimelist);
	mime_fetch_headers(raw_message, &mimelist);

	result = mail_address_build_list("Cc", &targetlist, &mimelist);
	struct element *el;
	el = targetlist.start;

	fail_unless(result==0, "mail_address_build_list failed");
	fail_unless(targetlist.total_nodes==2,"mail_address_build_list failed");
	fail_unless(strcmp((char *)el->data,"nobody@test123.com")==0, "mail_address_build_list failed");
}
END_TEST

//int db_fetch_headers(u64_t msguid, mime_message_t * msg)
START_TEST(test_db_fetch_headers)
{
	mime_message_t *msg = g_new0(mime_message_t,1);
	int res;
	
	//res = db_start_msg(msg,stopbound,&level,maxlevel);
	fail_unless(res==29, "db_start_msg result incorrect");
	fail_unless(msg->rfcheader.total_nodes == 10, "total-nodes for rfcheader incorrect");

	g_free(msg);

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
	

