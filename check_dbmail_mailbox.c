/*
 *  Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
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
 *  $Id: check_dbmail_deliver.c 1829 2005-08-01 14:53:53Z paul $ 
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

#include <check.h>
#include "check_dbmail.h"

extern char * multipart_message;
extern char * configFile;
extern db_param_t _db_params;
#define DBPFX _db_params.pfx



/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
	
static void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}

static u64_t get_mailbox_id(void)
{
	u64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox("INBOX", BOX_COMMANDLINE, owner, &id);
	return id;
}

void setup(void)
{
	configure_debug(5,0);
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


/****************************************************************************************
 *
 *
 * TestCases
 *
 *
 ***************************************************************************************/

START_TEST(test_dbmail_mailbox_new)
{
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	fail_unless(mb!=NULL, "dbmail_mailbox_new failed");
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_free)
{
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_open)
{
	int result;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	result = dbmail_mailbox_open(mb);
	fail_unless(result == 0, "dbmail_mailbox_open failed");
}
END_TEST

START_TEST(test_dbmail_mailbox_dump)
{
	int c = 0;
	FILE *o = fopen("/dev/null","w");
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	c = dbmail_mailbox_dump(mb,o);
	fail_unless(c>=0,"dbmail_mailbox_dump failed");
	dbmail_mailbox_free(mb);
//	fprintf(stderr,"dumped [%d] messages\n", c);
}
END_TEST

START_TEST(test_dbmail_mailbox_build_imap_search)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	struct DbmailMailbox *mb, *mc, *md;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM paul@nfg.nl SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);

	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	// second case
	idx = 0;
	mc = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival ) ( cc ) us-ascii HEADER FROM paul@nfg.nl "
			"SINCE 1-Feb-1990");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(mc, array, &idx, sorted);
	
	dbmail_mailbox_free(mc);
	g_strfreev(array);
	
	// third case
	idx = 0;
	md = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(md, array, &idx, sorted);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_strfreev(array);

	// fourth case
	idx = 0;
	md = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1,* ( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(md, array, &idx, 1);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_sort)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	struct DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii HEADER FROM test SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_sort(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_search)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;
	int all, found, notfound;
	struct DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM foo SINCE 1-Feb-1994 ( SENTSINCE 1-Feb-1995 OR BEFORE 1-Jan-2006 SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_sort(mb);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	// second case
	//
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1:*");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	all = g_tree_nnodes(mb->ids);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1:* TEXT @");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	found = g_tree_nnodes(mb->ids);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1:* NOT TEXT @");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	notfound = g_tree_nnodes(mb->ids);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	printf ("all [%d], found [%d], notfound [%d]", all, found, notfound);
	
	return;
	// third case
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1 BODY paul@nfg.nl");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	
}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_1)
{
	u64_t idx=0;
	gboolean sorted = 0;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	char *args = g_strdup("UID 1 BODY unlikelyaddress@nfg.nl");
	char **array = g_strsplit(args," ",0);
	g_free(args);
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_2)
{
	u64_t idx=0;
	gboolean sorted = 0;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	char *args = g_strdup("UID 1,* BODY the");
	char **array = g_strsplit(args," ",0);
	g_free(args);
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

const char * test_fetch_commands[12] = {
	"1:* ( UID BODY [ ] )",
        "1:* ( UID RFC822 )",
        "1:* ( UID BODY [ TEXT ] )",
        "1:* ( UID BODYSTRUCTURE )",
        "1:* ( UID BODY [ TEXT ] <0.20> )",
        "1:* ( UID BODY.PEEK [ TEXT ] <0.30> )",
        "1:* ( UID RFC822.SIZE )",
        "1:* ( UID RFC822.HEADER )",
        "1:* ( BODY.PEEK [ HEADER.FIELDS ( References X-Ref X-Priority X-MSMail-Priority X-MSOESRec Newsgroups ) ] ENVELOPE RFC822.SIZE UID FLAGS INTERNALDATE )",
        "1:* ( UID RFC822.SIZE FLAGS BODY.PEEK [ HEADER.FIELDS ( From To Cc Subject Date Message-ID Priority X-Priority References Newsgroups In-Reply-To Content-Type ) ] )",
        "1:* ( UID FULL )",
	NULL };
		

static void bodyfetch_set_partspec(body_fetch_t *self, char *partspec, int length) 
{
	assert(self);
	memset(self->partspec,'\0',IMAP_MAX_PARTSPEC_LEN);
	memcpy(self->partspec,partspec,length);
}

static char *bodyfetch_get_partspec(body_fetch_t *self) 
{
	assert(self);
	return self->partspec;
}

static void bodyfetch_set_itemtype(body_fetch_t *self, int itemtype) 
{
	assert(self);
	self->itemtype = itemtype;
}

static int bodyfetch_get_last_itemtype(body_fetch_t *self) 
{
	assert(self);
	return self->itemtype;
}
static void bodyfetch_set_argstart(body_fetch_t *self, int idx) 
{
	assert(self);
	self->argstart = idx;
}
static int bodyfetch_get_last_argstart(body_fetch_t *self) 
{
	assert(self);
	return self->argstart;
}
static void bodyfetch_set_argcnt(body_fetch_t *self, int idx) 
{
	assert(self);
	self->argcnt = idx - self->argstart;
}
static int bodyfetch_get_last_argcnt(body_fetch_t *self) 
{
	assert(self);
	return self->argcnt;
}
static void bodyfetch_set_octetstart(body_fetch_t *self, guint64 octet)
{
	assert(self);
	self->octetstart = octet;
}
static guint64 bodyfetch_get_octetstart(body_fetch_t *self)
{
	assert(self);
	return self->octetstart;
}
static void bodyfetch_set_octetcnt(body_fetch_t *self, guint64 octet)
{
	assert(self);
	self->octetcnt = octet;
}
static guint64 bodyfetch_get_last_octetcnt(body_fetch_t *self)
{
	assert(self);
	return self->octetcnt;
}


static int bodyfetch_parse_partspec(body_fetch_t *self, char **args, u64_t *idx)
{
	/* check for a partspecifier */
	/* first check if there is a partspecifier (numbers & dots) */
	int indigit = 0;
	unsigned int j = 0;
	char *token, *nexttoken;

	token=args[*idx];
	nexttoken=args[*idx+1];

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
		bodyfetch_set_partspec(self, token, j);
	}

	char *partspec = &token[j];


	int shouldclose = 0;

	if (MATCH(partspec, "header.fields")) {
		bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS);
	} else if (MATCH(partspec, "header.fields.not")) {
		bodyfetch_set_itemtype(self, BFIT_HEADER_FIELDS_NOT);
	} else if (MATCH(partspec, "text")) {
		bodyfetch_set_itemtype(self, BFIT_TEXT);
		shouldclose = 1;
	} else if (MATCH(partspec, "header")) {
		bodyfetch_set_itemtype(self, BFIT_HEADER);
		shouldclose = 1;
	} else if (MATCH(partspec, "mime")) {
		if (j == 0)
			return -2;	/* error DONE */

		bodyfetch_set_itemtype(self, BFIT_MIME);
		shouldclose = 1;
	} else if (token[j] == '\0') {
		bodyfetch_set_itemtype(self, BFIT_TEXT_SILENT);
		shouldclose = 1;
	} else {
		return -2;	/* error DONE */
	}
	
	if (shouldclose) {
		if (! MATCH(nexttoken, "]"))
			return -2;	/* error DONE */
	} else {
		(*idx)++;	/* should be at '(' now */
		token = args[*idx];
		nexttoken = args[*idx+1];
		
		if (! MATCH(token,"("))
			return -2;	/* error DONE */

		(*idx)++;	/* at first item of field list now, remember idx */
		bodyfetch_set_argstart(self, *idx);

		/* walk on until list terminates (and it does 'cause parentheses are matched) */
		while (! MATCH(args[*idx],")") )
			(*idx)++;

		token = args[*idx];
		nexttoken = args[*idx+1];
		
		bodyfetch_set_argcnt(self, *idx);

		if (bodyfetch_get_last_argcnt(self) == 0 || ! MATCH(nexttoken,"]") )
			return -2;	/* error DONE */
	}
	(*idx)++;
	return 0;
}

static int bodyfetch_parse_octet_range(body_fetch_t *self, char **args, u64_t *idx) 
{
	/* check if octet start/cnt is specified */
	int delimpos;
	unsigned int j = 0;
	
	char *token = args[*idx];
	
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
		bodyfetch_set_octetstart(self, strtoll(&token[1], NULL, 10));
		bodyfetch_set_octetcnt(self,strtoll(&token [delimpos + 1], NULL, 10));

		/* restore argument */
		token[delimpos] = '.';
		token[strlen(token) - 1] = '>';
	} else {
		return 0;
	}

	(*idx)++;
	return 0;
}

static void fetchitems_free(fetch_items_t *self)
{
	assert(self);
	if (self->bodyfetch)
		g_list_foreach(self->bodyfetch, (GFunc)g_free, NULL);
	g_free(self);
}

static int _build_fetch(struct DbmailMailbox *self, char **args, u64_t *idx)
{
	int ispeek = 0;
	char *token = NULL, *nexttoken = NULL;
	fetch_items_t *fi;
	
	if (!args[*idx])
		return 0;	/* no more */

	if (args[*idx][0] == '(')
		(*idx)++;

	if (!args[*idx])
		return -1;	/* error */

	fi = self->fi;
	
	token = args[*idx];
	nexttoken = args[*idx+1];
	
	trace(TRACE_DEBUG,"%s,%s: parse args[%llu] = [%s]",
		__FILE__,__func__, *idx, token);

	if (MATCH(token,"uid")) {
		dbmail_mailbox_set_uid(self,TRUE);
	} else if (check_msg_set(token)) {
		dbmail_mailbox_get_set(self, token);
	} else if (MATCH(token,"flags")) {
		fi->getFlags = 1;
	} else if (MATCH(token,"internaldate")) {
		fi->getInternalDate=1;
	} else if (MATCH(token,"uid")) {
		fi->getUID=1;
	} else if (MATCH(token,"rfc822.size")) {
		fi->getSize = 1;
	} else if (MATCH(token,"fast")) {
		fi->getInternalDate = 1;
		fi->getFlags = 1;
		fi->getSize = 1;
		
	/* from here on message parsing will be necessary */
	
	} else if (MATCH(token,"rfc822")) {
		fi->getRFC822=1;
	} else if (MATCH(token,"rfc822.header")) {
		fi->getRFC822Header = 1;
	} else if (MATCH(token,"rfc822.peek")) {
		fi->getRFC822Peek = 1;
	} else if (MATCH(token,"rfc822.text")) {
		fi->getRFC822Text = 1;
	} else if (MATCH(token,"bodystructure")) {
		fi->getMIME_IMB = 1;
	} else if (MATCH(token,"envelope")) {
		fi->getEnvelope = 1;
	} else if (MATCH(token,"all")) {		
		fi->getInternalDate = 1;
		fi->getEnvelope = 1;
		fi->getFlags = 1;
		fi->getSize = 1;
	} else if (MATCH(token,"full")) {
		fi->getInternalDate = 1;
		fi->getEnvelope = 1;
		fi->getMIME_IMB = 1;
		fi->getFlags = 1;
		fi->getSize = 1;

	} else if (MATCH(token,"body") || MATCH(token,"body.peek")) {
		
		body_fetch_t *bodyfetch = g_new0(body_fetch_t, 1);
		bodyfetch->itemtype = -1;
		fi->bodyfetch = g_list_append(fi->bodyfetch, bodyfetch);

		if (MATCH(token,"body.peek"))
			ispeek=1;
		
		if (! nexttoken || ! MATCH(nexttoken,"[")) {
			if (ispeek)
				return -2;	/* error DONE */
			fi->getMIME_IMB_noextension = 1;	/* just BODY specified */
		} else {
			/* now read the argument list to body */
			(*idx)++;	/* now pointing at '[' (not the last arg, parentheses are matched) */
			(*idx)++;	/* now pointing at what should be the item type */

			token = (char *)args[*idx];
			nexttoken = (char *)args[*idx+1];

			if (MATCH(token,"]")) {
				if (ispeek)
					fi->getBodyTotalPeek = 1;
				else
					fi->getBodyTotal = 1;
				(*idx)++;
				return bodyfetch_parse_octet_range(bodyfetch, args, idx);
			}
			
			if (ispeek)
				fi->noseen = 1;
			if ((bodyfetch_parse_partspec(bodyfetch ,args, idx)) < 0) {
				trace(TRACE_DEBUG,"%s,%s: bodyfetch_parse_partspec return with error", 
						__FILE__, __func__);
				return -1;
			}
			/* idx points to ']' now */
			(*idx)++;
			return bodyfetch_parse_octet_range(bodyfetch, args, idx);
		}
	} else {			
		if ((! nexttoken) && (strcmp(token,")") == 0)) {
			/* only allowed if last arg here */
			return 0;
		}
		return -1;	/* unknown key */

	}

	(*idx)++;
	trace(TRACE_DEBUG, "%s,%s: args[%llu]", __FILE__,__func__, *idx);
	return 1;
}

static int dbmail_mailbox_fetch_build(struct DbmailMailbox *self, char **args)
{
	u64_t idx=0;
	int result=0;
	
	if (self->fi)
		fetchitems_free(self->fi);
	self->fi = g_new0(fetch_items_t,1);
	
	while ((result = _build_fetch(self, args, &idx)) > 0)
		;

	return result;
}

static int dbmail_mailbox_fetch(struct DbmailMailbox *self)
{
	fetch_items_t *fi = self->fi;

	return 0;
}

START_TEST(test_dbmail_mailbox_fetch_build)
{
	int i=0, result;
	char **array;
	const char *args;

	struct DbmailMailbox *mb;
	mb = dbmail_mailbox_new(get_mailbox_id());

	while ((args = test_fetch_commands[i++])) {
		array = g_strsplit(args," ",0);
		result = dbmail_mailbox_fetch_build(mb, array);
		fail_unless(result==0,"dbmail_mailbox_fetch_build failed");
		g_strfreev(array);
	}

}
END_TEST

/* unfinished code: */
START_TEST(test_dbmail_mailbox_fetch)
{
	const char *args;
	char **array;
	int cmd = 0;

	struct DbmailMailbox *mb;
	mb = dbmail_mailbox_new(get_mailbox_id());
	
	args = test_fetch_commands[cmd++];
	array = g_strsplit(args," ",0);
	dbmail_mailbox_fetch_build(mb, array);
	dbmail_mailbox_fetch(mb);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	array = g_strsplit(args," ",0);
	g_strfreev(array);
	
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_orderedsubject)
{
	char *res;
	char *args;
	char **array;
	u64_t idx = 0;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	
	dbmail_mailbox_open(mb);

	args = g_strdup("HEADER FROM test ( SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(mb, array, &idx, 0);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_set_uid(mb,TRUE);
	res = dbmail_mailbox_orderedsubject(mb);
	g_free(res);
	//printf("threads [%s]\n", res);
	
	dbmail_mailbox_set_uid(mb,FALSE);
	res = dbmail_mailbox_orderedsubject(mb);
	g_free(res);
	//printf("threads [%s]\n", res);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

}
END_TEST
START_TEST(test_dbmail_mailbox_get_set)
{
	guint c, d;
	GTree *set;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	dbmail_mailbox_set_uid(mb,TRUE);

	set = dbmail_mailbox_get_set(mb, "1:*");
	c = g_tree_nnodes(set);
	fail_unless(c>1,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"*:1");
	d = g_tree_nnodes(set);
	fail_unless(c==d,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"1,*");
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"1,*");
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed");
	g_tree_destroy(set);
	
	set = dbmail_mailbox_get_set(mb,"1");
	d = g_tree_nnodes(set);
	fail_unless(d==1,"mailbox_get_set failed");
	g_tree_destroy(set);

	dbmail_mailbox_free(mb);
}
END_TEST

Suite *dbmail_mailbox_suite(void)
{
	Suite *s = suite_create("Dbmail Mailbox");

	TCase *tc_mailbox = tcase_create("Mailbox");
	suite_add_tcase(s, tc_mailbox);
	tcase_add_checked_fixture(tc_mailbox, setup, teardown);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_get_set);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_new);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_free);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_open);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_dump);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_build_imap_search);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_sort);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search_parsed_1);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search_parsed_2);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_orderedsubject);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_fetch_build);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_fetch);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_mailbox_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

