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

static u64_t get_first_user_idnr(void)
{
	u64_t user_idnr;
	GList *users = auth_get_known_users();
	users = g_list_first(users);
	auth_user_exists((char *)users->data,&user_idnr);
	return user_idnr;
}

static u64_t get_mailbox_id(void)
{
	u64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox("INBOX", owner, &id);
	return id;
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
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	mb = dbmail_mailbox_open(mb);
	fail_unless(mb!=NULL, "dbmail_mailbox_open failed");
}
END_TEST

START_TEST(test_dbmail_mailbox_dump)
{
	int c = 0;
//	FILE *o = fopen("/dev/null","w");
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	c = dbmail_mailbox_dump(mb,stdout);
	dbmail_mailbox_free(mb);
//	fprintf(stderr,"dumped [%d] messages\n", c);
}
END_TEST

void sl_print(search_key_t *sk) 
{
	printf("%s: search:[%s]\n", __func__, sk->search);
}

gboolean _search_tree_dump(GNode *node, struct DbmailMailbox *mb UNUSED)
{
	
	search_key_t *sk = (search_key_t *)node->data;
	
	if (! sk)
		return TRUE;

	switch(sk->type) {
		case IST_SORT:
			printf("%s: table:[%s]\n", __func__, sk->table);
			printf("%s: order:[%s]\n", __func__, sk->order);
		break;
		
		default:
			printf("%s: depth[%d] type[%d] hdrfld[%s] search[%s]\n", __func__, g_node_depth(node), sk->type, sk->hdrfld, sk->search);
		break;
	}
	
	return FALSE;
}
	
/* imap sorted search */

static search_key_t * append_search(struct DbmailMailbox *self, search_key_t *value, gboolean descend)
{
	GNode *n;
	
	if (self->search)
		n = g_node_append_data(self->search, value);
	else {
		descend = TRUE;
		n = g_node_new(value);
	}
	
	if (descend)
		self->search = n;
	
	trace(TRACE_DEBUG, "%s,%s: [%d] [%d] type [%d] field [%s] search [%s] at depth [%u]\n", 
			__FILE__, __func__, (int)value, descend, 
			value->type, value->hdrfld, value->search, 
			g_node_depth(self->search));
	return g_new0(search_key_t,1);
}

static void _append_join(char *join, char *table)
{
	char *tmp;
	tmp = g_strdup_printf("LEFT JOIN %s%s ON p.id=%s%s.physmessage_id ", DBPFX, table, DBPFX, table);
	g_strlcat(join, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}

static void _append_sort(char *order, char *field, gboolean reverse)
{
	char *tmp;
	tmp = g_strdup_printf("%s%s,", field, reverse ? " DESC" : "");
	g_strlcat(order, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}

static int _handle_sort_args(struct DbmailMailbox *self, char **search_keys, search_key_t *value, u64_t *idx)
{
	value->type = IST_SORT;
			
	gboolean reverse = FALSE;

	g_return_val_if_fail(search_keys && search_keys[*idx], -1);

	char *key = search_keys[*idx];
	
	if ( MATCH(key, "reverse") ) {
		reverse = TRUE;
		(*idx)++;
		key = search_keys[*idx];
	} 
	
	if ( MATCH(key, "arrival") ) {
		_append_sort(value->order, "internal_date", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "size") ) {
		_append_sort(value->order, "messagesize", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "from") ) {
		_append_join(value->table, "fromfield");
		_append_sort(value->order, "fromaddr", reverse);	
		(*idx)++;
	} 
	
	else if ( MATCH(key, "subject") ) {
		_append_join(value->table, "subjectfield");
		_append_sort(value->order, "subjectfield", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "cc") ) {
		_append_join(value->table, "ccfield");
		_append_sort(value->order, "ccaddr", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "to") ) {
		_append_join(value->table, "tofield");
		_append_sort(value->order, "toaddr", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "date") ) {
		_append_join(value->table, "datefield");
		_append_sort(value->order, "datefield", reverse);
		(*idx)++;
	}	

	else if ( MATCH(key, "(") )
		(*idx)++;

	else if ( MATCH(key, ")") ) 
		(*idx)++;
	
	else if ( MATCH(key, "utf-8") ) 
		(*idx)++;
	
	else if ( MATCH(key, "us-ascii") ) 
		(*idx)++;
	
	else if ( MATCH(key, "iso-8859-1") ) 
		(*idx)++;

	else {
		if (value->type)
			value = append_search(self, value, 0);
		return 1; /* done */
	}

	return 0;
}

static void pop_search(struct DbmailMailbox *self)
{
	// switch back to parent 
	if (self->search && self->search->parent) 
		self->search = self->search->parent;
}

static int _handle_search_args(struct DbmailMailbox *self, char **search_keys, u64_t *idx)
{
	int result = 0;

	g_return_val_if_fail(search_keys && search_keys[*idx], -1);

	char *key = search_keys[*idx];

	search_key_t *value = g_new0(search_key_t,1);
	
	/* SEARCH */

	if ( MATCH(key, "all") ) {
		value->type = IST_SET;
		strcpy(value->search, "1:*");
		(*idx)++;
		
	} 
	
	else if ( MATCH(key, "uid") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_msg_set(search_keys[*idx + 1]),-1);
		value->type = IST_SET_UID;
		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * FLAG search keys
	 */

	else if ( MATCH(key, "answered") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "deleted") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "flagged") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "recent") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "seen") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "keyword") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SET;
		(*idx)++;
		strcpy(value->search, "0");
		(*idx)++;
		
	} else if ( MATCH(key, "draft") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "new") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "(seen_flag=0 AND recent_flag=1)", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "old") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "unanswered") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN);
		(*idx)++;

	} else if ( MATCH(key, "undeleted") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unflagged") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unseen") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unkeyword") ) {
		g_return_val_if_fail(search_keys[(*idx) + 1],-1);
		value->type = IST_SET;
		(*idx)++;
		strcpy(value->search, "1:*");
		(*idx)++;
	
	} else if ( MATCH(key, "undraft") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * HEADER search keys
	 */

	else if ( MATCH(key, "bcc") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "bcc", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "cc") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "cc", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "from") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "from", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "to") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "to", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "subject") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "subject", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "header") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(search_keys[*idx + 2], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, search_keys[*idx + 1], MIME_FIELD_MAX);
		strncpy(value->search, search_keys[*idx + 2], MAX_SEARCH_LEN);
		(*idx) += 3;

	} else if ( MATCH(key, "sentbefore") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_BEFORE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;

	} else if ( MATCH(key, "senton") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_ON;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "sentsince") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_SINCE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		(*idx)++;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * INTERNALDATE keys
	 */

	else if ( MATCH(key, "before") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date < '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if ( MATCH(key, "on") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date LIKE '%s%%'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if ( MATCH(key, "since") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date > '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
	}

	/*
	 * DATA-keys
	 */

	else if ( MATCH(key, "body") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_DATA_BODY;
		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "text") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_DATA_TEXT;
		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * SIZE keys
	 */

	else if ( MATCH(key, "larger") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_LARGER;
		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	} else if ( MATCH(key, "smaller") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_SMALLER;
		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	}

	/*
	 * NOT, OR, ()
	 */
	
	else if ( MATCH(key, "not") ) {
		value->type = IST_SUBSEARCH_NOT;
		(*idx)++;
	
		if ((result = dbmail_mailbox_build_imap_search(self, search_keys, idx, 0)) < 0) 
			return result;
		
	} else if ( MATCH(key, "or") ) {
		value->type = IST_SUBSEARCH_OR;
		(*idx)++;
		
		value = append_search(self, value, 1);
		if ((result = _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		if ((result =  _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		pop_search(self);
		
		return result;

	} else if ( MATCH(key, "(") ) {
		value->type = IST_SUBSEARCH_AND;
		(*idx)++;
		
		value = append_search(self,value,1);
		while ((result = dbmail_mailbox_build_imap_search(self, search_keys, idx, 0)) == 0);
		pop_search(self);
		
		return result;

	} else if ( MATCH(key, ")") ) {
		(*idx)++;
		
		return 1;
	
	} else if (check_msg_set(key)) {
		value->type = IST_SET;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else {
		/* unknown search key */
		return -1;
	}
	
	value = append_search(self, value, 0);

	return 0;
}

/*
 * build_imap_search()
 *
 * builds a linked list of search items from a set of IMAP search keys
 * sl should be initialized; new search items are simply added to the list
 *
 * returns -1 on syntax error, -2 on memory error; 0 on success, 1 if ')' has been encountered
 */
int dbmail_mailbox_build_imap_search(struct DbmailMailbox *self, char **search_keys, u64_t *idx, int sorted)
{
	int result = 0;
	search_key_t * value;
	
	if (! (search_keys && search_keys[*idx]))
		return 1;

	/* SORT */
	if (sorted) {
		value = g_new0(search_key_t,1);
		while(((result = _handle_sort_args(self, search_keys, value, idx)) == 0) && search_keys[*idx]);
	}

	/* SEARCH */
	while(((result = _handle_search_args(self, search_keys, idx)) == 0) && search_keys[*idx]);
	
	return result;
}


START_TEST(test_dbmail_mailbox_build_imap_search)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	search_key_t *sk = NULL, *save;
	struct DbmailMailbox *mb, *mc, *md;
	
	// first case
	save = g_new0(search_key_t,1);
	mb = dbmail_mailbox_new(get_mailbox_id());
	
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii HEADER FROM paul@nfg.nl SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	sk = save;
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);

	dbmail_mailbox_free(mb);
	g_free(save);
	g_strfreev(array);
	
	// second case
	sk = NULL;
	idx = 0;
	save = g_new0(search_key_t,1);
	mc = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival ) ( cc ) us-ascii HEADER FROM paul@nfg.nl SINCE 1-Feb-1990");
	array = g_strsplit(args," ",0);
	g_free(args);

	sk = save;
	dbmail_mailbox_build_imap_search(mc, array, &idx, sorted);

	
	dbmail_mailbox_free(mc);
	g_free(save);
	g_strfreev(array);
	
	// third case
	sk = NULL;
	idx = 0;
	save = g_new0(search_key_t,1);
	md = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test HEADER SUBJECT foo )");
	
	array = g_strsplit(args," ",0);
	g_free(args);

	sk = save;
	dbmail_mailbox_build_imap_search(md, array, &idx, sorted);

	//g_node_traverse(g_node_get_root(md->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1, (GNodeTraverseFunc)_search_tree_dump, (gpointer)md);
	
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, "build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_free(save);
	g_strfreev(array);
}
END_TEST

static gint ucmp(gconstpointer a, gconstpointer b)
{
	unsigned x,y;
	x = GPOINTER_TO_UINT(a);
	y = GPOINTER_TO_UINT(b);
	
	if (x>y)
		return 1;
	if (x==y)
		return 0;
	return -1;
}

static gboolean _show_tree(gpointer key, gpointer value, gpointer data UNUSED)
{
	printf("[%u] -> [%u]\n", GPOINTER_TO_UINT(key), GPOINTER_TO_UINT(value));
	return FALSE;
}

static gboolean _do_sort(GNode *node, struct DbmailMailbox *self)
{
	GString *q;
	u64_t id;
	unsigned i, rows;
	search_key_t *s = (search_key_t *)node->data;
	
	if (s->type != IST_SORT)
		return TRUE;
	
	
	q = g_string_new("");
	g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "LEFT JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "%s"
			 "WHERE m.mailbox_idnr = '%llu' AND m.status IN ('%d','%d') " 
			 "ORDER BY %smessage_idnr", DBPFX, DBPFX, s->table,
			 dbmail_mailbox_get_id(self), MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, s->order);

	if (db_query(q->str) == -1)
		return TRUE;

	self->sorted = g_tree_new((GCompareFunc)ucmp);
	
	rows = db_num_rows();
	for (i=0; i< rows; i++) {
		id = db_get_result_u64(i,0);
		g_tree_insert(self->sorted, GUINT_TO_POINTER((unsigned)id), GUINT_TO_POINTER((unsigned)id));
	}
	g_string_free(q,TRUE);
	db_free_result();
	
	return FALSE;
}

static gboolean tree_keys(gpointer key, gpointer value UNUSED, GList **keys)
{
	*(GList **)keys = g_list_append(*(GList **)keys, key);
	return FALSE;
}

static void tree_merge(GTree *a, GTree *b, int condition)
{
	gpointer value;	
	GList *akeys = NULL;
	GList *bkeys = NULL;
	
	if (a)
		g_tree_foreach(a, (GTraverseFunc)tree_keys, &akeys);
	if (b)
		g_tree_foreach(b, (GTraverseFunc)tree_keys, &bkeys);
	
	akeys = g_list_first(akeys);
	bkeys = g_list_first(bkeys);
	

	printf ("%s: combine type [%d], a[%d], b[%d] ...", __func__, condition, g_list_length(akeys), g_list_length(bkeys));
	switch(condition) {
		case IST_SUBSEARCH_AND:
			/* delete from A all keys not in B */
			if (g_list_length(akeys)) {
				while (akeys->data) {
					if ( (! b) || (! g_tree_lookup(b,akeys->data)) )
						g_tree_steal(a,akeys->data);

					if (! g_list_next(akeys))
						break;
					
					akeys = g_list_next(akeys);
				}
			}
			break;
			
		case IST_SUBSEARCH_OR:
			/* add to A all keys in B */
			if (g_list_length(bkeys)) {
				while (bkeys->data) {
					value = g_tree_lookup(b,bkeys->data);
					g_tree_insert(a,bkeys->data,value);

					if (! g_list_next(bkeys))
						break;
					
					bkeys = g_list_next(bkeys);
				}
			}
			break;
			
		case IST_SUBSEARCH_NOT:
			/* remove from A all keys also in B */
			if (g_list_length(akeys)) {
				while (akeys->data) {
					if ( b && g_tree_lookup(b,akeys->data))
						g_tree_steal(a,akeys->data);

					if (! g_list_next(akeys))
						break;

					akeys = g_list_next(akeys);
				}
			}
			/* add to A all keys in B not in A */
			if (g_list_length(bkeys)) {
				while (bkeys->data) {
					value = g_tree_lookup(b,bkeys->data);
					if (! g_tree_lookup(a,bkeys->data))
						g_tree_insert(a,bkeys->data,value);

					if (! g_list_next(bkeys))
						break;

					bkeys = g_list_next(bkeys);
				}
			}
				
			break;
	}

	printf(" result[%d]\n", a ? g_tree_nnodes(a): 0);

	g_list_free(akeys);
	g_list_free(bkeys);
			
}

static GTree * _db_search(struct DbmailMailbox *self, search_key_t *s)
{
	unsigned i, rows, date, id;
	
	GString *t = g_string_new("");
	GString *q = g_string_new("");

	if (!s->search)
		return NULL;

	switch (s->type) {
		case IST_HDRDATE_ON:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_BEFORE:

		date = num_from_imapdate(s->search);
		if (s->type == IST_HDRDATE_SINCE)
			g_string_printf(t,"%s >= %d", s->hdrfld, date);
		else if (s->type == IST_HDRDATE_BEFORE)
			g_string_printf(t,"%s < %d", s->hdrfld, date);
		else
			g_string_printf(t,"%s >= %d AND %s < %d", s->hdrfld, date, s->hdrfld, date+1);
		
		g_string_printf(q,"SELECT message_idnr FROM %smessages m "
			"JOIN %sphysmessage p ON m.physmessage_id=p.id "
			"JOIN %sdatefield d ON d.physmessage_id=p.id "
			"WHERE mailbox_idnr= %llu AND status IN ('%d','%d') "
			"AND %s", DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self), 
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			t->str);
			break;
			
		case IST_HDR:
		
		g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "JOIN %sheadervalue v ON v.physmessage_id=p.id "
			 "JOIN %sheadername n ON v.headername_id=n.id "
			 "WHERE mailbox_idnr = %llu "
			 "AND status IN ('%d','%d') "
			 "AND headername = '%s' AND headervalue LIKE '%%%s%%'", 
			 DBPFX, DBPFX, DBPFX, DBPFX,
			 dbmail_mailbox_get_id(self), 
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			 s->hdrfld, s->search);
			break;

		case IST_IDATE:
			
		g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "WHERE mailbox_idnr = '%llu' "
			 "AND status IN ('%d','%d') AND p.%s", 
			 DBPFX, DBPFX, 
			 dbmail_mailbox_get_id(self),
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			 s->search);
		break;
		
		default:
		g_string_printf(q, "SELECT message_idnr FROM %smessages "
			 "WHERE mailbox_idnr = '%llu' "
			 "AND status IN ('%d','%d') AND %s", DBPFX, 
			 dbmail_mailbox_get_id(self), 
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			 s->search);
		break;
		
	}
	
	g_string_free(t,TRUE);
	if (db_query(q->str) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		g_string_free(q,TRUE);
		return NULL;
	}
	g_string_free(q,TRUE);

	rows = db_num_rows();
	if (rows > 0) {
		s->found = g_tree_new((GCompareFunc)ucmp);
		for (i=0; i < rows; i++) {
			id = db_get_result_u64(i,0);
			g_tree_insert(s->found, GUINT_TO_POINTER((unsigned)id), GUINT_TO_POINTER((unsigned)id));
		}
	}
	
	db_free_result();
	return s->found;
}


static gboolean _do_search(GNode *node, struct DbmailMailbox *self)
{
	search_key_t *s = (search_key_t *)node->data;
	GTree *set = NULL;
	

	switch (s->type) {
		case IST_SET:
		//	build_set(rset, setlen, sk->search);
			break;

		case IST_SET_UID:
		//	build_uid_set(rset, setlen, sk->search, mb);
			break;

		case IST_SORT:
			break;

		case IST_SIZE_LARGER:
		case IST_SIZE_SMALLER:
		case IST_HDRDATE_BEFORE:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_ON:
		case IST_IDATE:
		case IST_FLAG:
		case IST_HDR:
			if (! (set = _db_search(self, s)))
				return TRUE;
			break;
		/* 
		 * these all have in common that all messages need to be parsed 
		 */
		case IST_DATA_BODY:
		case IST_DATA_TEXT:
			//result = db_search_parsed(rset, setlen, sk, mb, condition);
			break;

		case IST_SUBSEARCH_NOT:
		case IST_SUBSEARCH_AND:
		case IST_SUBSEARCH_OR:
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc)_do_search, (gpointer)self);
			break;

		default:
			return TRUE;
	}

	printf("%s: type [%d] rows [%d]\n", __func__, s->type, set ? g_tree_nnodes(set): 0);
	return FALSE;
}	

static int mailbox_sort(struct DbmailMailbox *self) 
{
	if (! self->search)
		return 0;
	
	g_node_traverse(g_node_get_root(self->search), G_IN_ORDER, G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc)_do_sort, (gpointer)self);
	
	return 0;
}

static gboolean _merge_search(GNode *node, GTree *found)
{
	search_key_t *s = (search_key_t *)node->data;
	search_key_t *a, *b;
	GNode *x, *y;

	switch(s->type) {
		case IST_SUBSEARCH_AND:
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer)node);
			break;
			
		case IST_SUBSEARCH_NOT:
			tree_merge(found, s->found, IST_SUBSEARCH_NOT);
			break;
			
		case IST_SUBSEARCH_OR:
			x = g_node_nth_child(node,0);
			y = g_node_nth_child(node,1);
			a = (search_key_t *)x->data;
			b = (search_key_t *)y->data;
			tree_merge(a->found,b->found,IST_SUBSEARCH_OR);
			tree_merge(found,a->found,IST_SUBSEARCH_AND);
			break;
			
		default:
			tree_merge(found, s->found, IST_SUBSEARCH_AND);
			break;
	}

	return FALSE;
}
static int mailbox_search(struct DbmailMailbox *self) 
{
	GString *q;
	unsigned i, rows, id;
	if (! self->search)
		return 0;
	
	if (! self->found) {
	 	q = g_string_new("");
		g_string_printf(q, "SELECT message_idnr FROM %smessages "
			 "WHERE mailbox_idnr = '%llu' "
			 "AND status IN ('%d','%d')", DBPFX, 
			 dbmail_mailbox_get_id(self), 
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);

		if (db_query(q->str) == -1)
			return -1;
		
		self->found = g_tree_new((GCompareFunc)ucmp);
		rows = db_num_rows();
		for (i=0; i < rows; i++) {
			id = db_get_result_u64(i,0);
			g_tree_insert(self->found, GUINT_TO_POINTER((unsigned)id), GUINT_TO_POINTER((unsigned)id));
		}
		db_free_result();
		g_string_free(q, TRUE);
	}
	
	g_node_children_foreach(g_node_get_root(self->search), G_TRAVERSE_ALL, (GNodeForeachFunc)_do_search, (gpointer)self);
	g_node_children_foreach(g_node_get_root(self->search), G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer)self->found);
	
	printf("%s: found [%d] ids\n", __func__, g_tree_nnodes(self->found));
	
	return 0;
}

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

	mailbox_sort(mb);
	
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
	struct DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM foo SINCE 1-Feb-1994 ( SENTSINCE 1-Feb-1995 OR BEFORE 1-Jan-2006 SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	
	mailbox_sort(mb);
	mailbox_search(mb);

	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_orderedsubject)
{
	char *res;
	unsigned setlen = 3;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	u64_t *rset = g_new0(u64_t, setlen);
	rset[0] = 1;
	rset[1] = 2;
	rset[2] = 1074946;
	res = dbmail_mailbox_orderedsubject(mb, rset, setlen);
//	printf("threads [%s]\n", res);

}
END_TEST

Suite *dbmail_mailbox_suite(void)
{
	Suite *s = suite_create("Dbmail Mailbox");

	TCase *tc_mailbox = tcase_create("Mailbox");
	suite_add_tcase(s, tc_mailbox);
	tcase_add_checked_fixture(tc_mailbox, setup, teardown);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_new);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_free);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_open);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_dump);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_build_imap_search);
//	tcase_add_test(tc_mailbox, test_dbmail_mailbox_sort);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_orderedsubject);
	
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
	

