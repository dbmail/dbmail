/*
  $Id: dbsearch.c 1756 2005-04-19 07:37:27Z paul $
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

/**
 * \file dbsearch.c
 * \brief functions implementing searching for messages
 *        the functions in this file used to be located in the 
 *        dbpgsql.c (PostgreSQL) and dbmysql (MySQL), but have
 *        been made backend-independent, so they can be used
 *        by any SQL database.
 */

#include "dbmail.h"

extern db_param_t _db_params;
#define DBPFX _db_params.pfx


/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

/* used only locally */
/**
 * \brief perform search on on the body of a message
 * \param msg mime_message_t struct of message
 * \param sk search key
 * \param msg_idnr
 * \return 
 *     - 0 if no match
 *     - 1 if match
 */
static int db_exec_search(GMimeObject *object, search_key_t * sk);

/**
 * \brief search the specified range of a message for a key
 * \param start of range
 * \param end of range
 * \param key key to search for
 * \param msg_idnr 
 * \return 
 *    - 0 if not found
 *    - 1 if found
 */
static int db_search_body(GMimeObject *object, search_key_t *sk);

int db_search(unsigned int *rset, unsigned setlen, search_key_t * sk, mailbox_t * mb)
{
	int msn;
	unsigned i;
	int date;
	
	GString *tmp = g_string_new("");

	if (!sk->search)
		return -2;

	
	memset(rset, 0, setlen * sizeof(int));

	switch (sk->type) {
		case IST_HDRDATE_ON:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_BEFORE:

		date = num_from_imapdate(sk->search);
		if (sk->type == IST_HDRDATE_SINCE)
			g_string_printf(tmp,"datefield >= %d", date);
		else if (sk->type == IST_HDRDATE_BEFORE)
			g_string_printf(tmp,"datefield < %d", date);
		else
			g_string_printf(tmp,"datefield >= %d AND datefield < %d", date, date+1);
		
		snprintf(query, DEF_QUERYSIZE,	
			"SELECT message_idnr FROM %smessages msg "
			"JOIN %sphysmessage phys ON msg.physmessage_id = phys.id "
			"JOIN %sdatefield df ON df.physmessage_id=phys.id "
			"WHERE mailbox_idnr= %llu AND msg.status < '%d' "
			"AND %s", DBPFX, DBPFX, DBPFX,
			mb->uid, MESSAGE_STATUS_DELETE, tmp->str);
			break;
			
		case IST_HDR:
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT message_idnr FROM %smessages msg "
			 "JOIN %sphysmessage phys ON msg.physmessage_id=phys.id "
			 "JOIN %sheadervalue hv ON hv.physmessage_id=phys.id "
			 "JOIN %sheadername hn ON hv.headername_id=hn.id "
			 "WHERE mailbox_idnr = %llu "
			 "AND msg.status < '%d' "
			 "AND headername = '%s' "
			 "AND headervalue LIKE '%%%s%%'", DBPFX, DBPFX, DBPFX, DBPFX,
			 mb->uid, MESSAGE_STATUS_DELETE, sk->hdrfld, sk->search);
			break;

		case IST_IDATE:
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT message_idnr FROM %smessages m "
			 "JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "WHERE m.mailbox_idnr = '%llu' "
			 "AND m.status < '%d' AND p.%s", DBPFX, DBPFX, 
			 mb->uid, MESSAGE_STATUS_DELETE, sk->search);
		break;
		
		case IST_SORT:
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT msg.message_idnr FROM %smessages msg, %sphysmessage pms "
			 "WHERE msg.mailbox_idnr = '%llu' "
			 "AND msg.physmessage_id = pms.id "
			 "AND msg.status < '%d' " 
			 "%s", DBPFX, DBPFX, 
			 mb->uid, MESSAGE_STATUS_DELETE, sk->search);
		break;
		
		case IST_SORT_FLD:
		snprintf(query, DEF_QUERYSIZE,
			"SELECT message_idnr FROM %smessages m "
			"JOIN %sphysmessage p on p.id=m.physmessage_id "
			"JOIN %s ft on p.id=ft.physmessage_id "
			"WHERE m.mailbox_idnr = '%llu' AND m.status < '%d' " 
			"ORDER BY %s %s,message_idnr",
			DBPFX,DBPFX, sk->table, mb->uid, 
			MESSAGE_STATUS_DELETE, 
			sk->field, sk->reverse ? "DESC" : "ASC");
		break;
		
		default:
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT message_idnr FROM %smessages "
			 "WHERE mailbox_idnr = '%llu' "
			 "AND status < '%d' AND %s", DBPFX, 
			 mb->uid, MESSAGE_STATUS_DELETE, sk->search);
		break;
		
	}
	
	g_string_free(tmp,TRUE);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return (-1);
	}

	for (i = 0; i < db_num_rows(); i++) {
		if (sk->type != IST_SORT) {
			msn = db_binary_search(mb->seq_list, mb->exists, db_get_result_u64(i, 0));
			if (msn == -1 || (unsigned)msn >= setlen) {
				db_free_result();
				return 1;
			}
			rset[msn] = 1;
		} else {
			rset[i] = (i + 1);
		}
	}

	db_free_result();
	return 0;
}

void addto_btree_curr(sortitems_t ** root, char *str, int mid)
{
	sortitems_t *curr = (sortitems_t *) dm_malloc(sizeof(sortitems_t));
	curr->left = curr->right = NULL;
	curr->mid = mid;
	curr->ustr = (char *) dm_malloc(sizeof(char) * (strlen(str) + 8));
	memset(curr->ustr, '\0', sizeof(char) * (strlen(str) + 8));
	sprintf(curr->ustr, "%s%06d", str, mid);
	dm_btree_insert(root, curr);
}

int db_sort_parsed(unsigned int *rset, unsigned int setlen,
		   search_key_t * sk, mailbox_t * mb, int condition)
{

	unsigned int i;
	int result, idx = 0;
	struct mime_record *mr;
	sortitems_t *root = NULL;
	struct dm_list hdrs;

	if (!sk->search)
		return 0;

	if (mb->exists != setlen)
		return 1;

	if (condition != IST_SUBSEARCH_AND && condition != IST_SUBSEARCH_NOT)
		memset(rset, 0, sizeof(int) * setlen);
	
	/*create a btree for all messages hdrfld */
	for (i = 0; i < setlen; i++) {

		if (condition == IST_SUBSEARCH_AND && rset[i] == 0)
			continue;
		if (condition == IST_SUBSEARCH_NOT && rset[i] == 1)
			continue;
		
		dm_list_init(&hdrs);
		
		if ((result = db_get_main_header(mb->seq_list[i], &hdrs, sk->hdrfld)))
			continue;	/* ignore parse errors */

		if (dm_list_getstart(&hdrs)) {
			mime_findfield(sk->hdrfld, &hdrs, &mr);
			if (mr)
				addto_btree_curr(&root, (char *) mr->value, (i + 1));
		}
		dm_list_free(&hdrs.start);
	}

	dm_btree_traverse(root, &idx, rset);	/* fill in the rset array with mid's */
	dm_btree_free(root);
    
	return 0;
}

int db_search_parsed(unsigned int *rset, unsigned int setlen,
		     search_key_t * sk, mailbox_t * mb, int condition)
{
	unsigned i;
	u64_t rfcsize;
	struct DbmailMessage *msg;

	if (mb->exists != setlen)
		return 1;

	if ((condition != IST_SUBSEARCH_AND && condition != IST_SUBSEARCH_NOT))
		memset(rset, 0, sizeof(int) * setlen);

	for (i = 0; i < setlen; i++) {

		if (condition == IST_SUBSEARCH_AND && rset[i] == 0)
			continue;
		if (condition == IST_SUBSEARCH_NOT && rset[i] == 1)
			continue;

		if (! (msg = db_init_fetch(mb->seq_list[i],DBMAIL_MESSAGE_FILTER_FULL)))
			continue;
		
		rfcsize = dbmail_message_get_rfcsize(msg);
		
		if (sk->type == IST_SIZE_LARGER)
			rset[i] = (rfcsize > sk->size) ? 1 : 0;
		
		else if (sk->type == IST_SIZE_SMALLER)
			rset[i] = (rfcsize < sk->size) ? 1 : 0;
		
		else
			rset[i] = db_exec_search(GMIME_OBJECT(msg->content), sk);

		dbmail_message_free(msg);
	}
	return 0;
}

static void _match_header(const char *field, const char *value, gpointer userdata)
{
	int i;
	search_key_t * sk = (search_key_t *)userdata;
	
	for (i = 0; field[i]; i++) {
		if (strncasecmp(&field[i], sk->search, strlen(sk->search)) == 0) {
			sk->match = 1;
			return;
		}
	}

	for (i = 0; value[i]; i++) {
		if (strncasecmp(&value[i], sk->search, strlen(sk->search)) == 0) {
			sk->match = 1;
			return;
		}
	}
	sk->match = 0;
}
	
int db_exec_search(GMimeObject *object, search_key_t * sk)
{
	int givendate, sentdate;
	char *d;
	int i,j;

	GMimeObject *part, *subpart;
	GMimeContentType *type;
	GMimeMultipart *multipart;
	
	if (!sk->search)
		return 0;

	switch (sk->type) {
		
	case IST_HDRDATE_BEFORE:
	case IST_HDRDATE_ON:
	case IST_HDRDATE_SINCE:
		d = g_mime_message_get_date_string(GMIME_MESSAGE(object));
		if (strlen(d) >= strlen("Day, d mon yyyy ")) {
			
			/* 01234567890123456 */
			
			givendate = num_from_imapdate(sk->search);

			if (d[6] == ' ')
				d[15] = 0;
			else
				d[16] = 0;

			sentdate = num_from_imapdate(&d[5]);

			switch (sk->type) {
			case IST_HDRDATE_BEFORE:
				return sentdate < givendate;
			case IST_HDRDATE_ON:
				return sentdate == givendate;
			case IST_HDRDATE_SINCE:
				return sentdate > givendate;
			}
		}
		return 0;

	case IST_DATA_TEXT:
		g_mime_header_foreach(object->headers, _match_header, (gpointer)sk);
		return sk->match;

	case IST_DATA_BODY:
		/* only check body if there are no children */
		if (GMIME_IS_MESSAGE(object)) {
			part = g_mime_message_get_mime_part(GMIME_MESSAGE(object));
		} else {
			part = object;
		}
		
		if (! (type = (GMimeContentType *)g_mime_object_get_content_type(part))) 
			break;
		
		if (! g_mime_content_type_is_type(type,"text","*"))
			break;
		
		return db_search_body(part, sk);
		
	}
	
	/* no match found yet, try the children */
	if (GMIME_IS_MESSAGE(object)) {
		part = g_mime_message_get_mime_part(GMIME_MESSAGE(object));
	} else {
		part = object;
	}
	
	if (! (type = (GMimeContentType *)g_mime_object_get_content_type(part)))
		return 0;
	
	if (! (g_mime_content_type_is_type(type,"multipart","*")))
		return 0;

	multipart = GMIME_MULTIPART(part);
	i = g_mime_multipart_get_number(multipart);
	
	trace(TRACE_DEBUG,"%s,%s: search [%d] parts for [%s]",
			__FILE__, __func__, i, sk->search);

	/* loop over parts for base info */
	for (j=0; j<i; j++) {
		subpart = g_mime_multipart_get_part(multipart,j);
		if (db_exec_search(subpart,sk) == 1)
			return 1;
	}
	
	return 0;
}

int db_search_body(GMimeObject *object, search_key_t *sk)
{
	int i;
	char *s;
	GString *t;
	
	s = g_mime_object_get_headers(object);
	i = strlen(s);
	g_free(s);

	s = g_mime_object_to_string(object);
	t = g_string_new(s);
	g_free(s);
	
	t = g_string_erase(t,0,i);
	s = t->str;
	g_string_free(t,FALSE);

	sk->match = 0;
	for (i = 0; s[i]; i++) {
		if (strncasecmp(&s[i], sk->search, strlen(sk->search)) == 0) {
			sk->match = 1;
			break;
		}
	}
	g_free(s);

	return sk->match;
}

