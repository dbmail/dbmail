/*
 Copyright (c) 2002 Aaron Stone, aaron@serendipity.cx

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
 * * User authentication functions for LDAP.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "assert.h"
#include "auth.h"
#include "dbmail.h"
#include "db.h"
#include "dbmd5.h"
#include "debug.h"
#include "list.h"
#include "misc.h"
#include <ldap.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <crypt.h>
#include <time.h>
#include <glib.h>

#define AUTH_QUERY_SIZE 1024
#define LDAP_RES_SIZE 1024

static char *configFile = DEFAULT_CONFIG_FILE;

LDAP *_ldap_conn;
LDAPMod **_ldap_mod;
LDAPMessage *_ldap_res;
LDAPMessage *_ldap_msg;
int _ldap_err;
int _ldap_attrsonly = 0;
char *_ldap_dn;
char **_ldap_vals;
char **_ldap_attrs = NULL;
char _ldap_query[AUTH_QUERY_SIZE];

typedef struct _ldap_cfg {
	field_t bind_dn, bind_pw, base_dn, port, scope, hostname, objectclass;
	field_t cn_string;
	field_t field_uid, field_cid, min_cid, max_cid, field_nid, min_nid, max_nid;
	field_t field_mail, field_mailalt, mailaltprefix;
	field_t field_maxmail, field_passwd;
	field_t field_fwd, field_fwdsave, field_fwdtarget, fwdtargetprefix;
	field_t field_members;
	int scope_int, port_int;
} _ldap_cfg_t;

_ldap_cfg_t _ldap_cfg;

/* Define a macro to cut down on code duplication... */
#define GETCONFIGVALUE(key, sect, var)		\
	config_get_value(key, sect, var);		\
	if (strlen(var) == 0)			\
		trace(TRACE_DEBUG, "%s, %s: no value for "	\
			#key " in config file section " #sect,	\
		    	__FILE__, __func__ );		\
	trace(TRACE_DEBUG, "%s, %s: value for "			\
		#key " from section " #sect " stored in "	\
		#var " as [%s]", __FILE__, __func__, var)
	/* No final ; so macro can be called "like a function" */

static void __auth_get_config(void);

static GList * __auth_get_every_match(const char *q, char **retfields);


void __auth_get_config(void)
{
	config_read(configFile);
	SetTraceLevel("LDAP");

	GETCONFIGVALUE("BIND_DN",	"LDAP", _ldap_cfg.bind_dn);
	GETCONFIGVALUE("BIND_PW",	"LDAP", _ldap_cfg.bind_pw);
	GETCONFIGVALUE("BASE_DN",	"LDAP", _ldap_cfg.base_dn);
	GETCONFIGVALUE("PORT",		"LDAP", _ldap_cfg.port);
	GETCONFIGVALUE("HOSTNAME",	"LDAP", _ldap_cfg.hostname);
	GETCONFIGVALUE("OBJECTCLASS",	"LDAP", _ldap_cfg.objectclass);
	GETCONFIGVALUE("CN_STRING",	"LDAP", _ldap_cfg.cn_string);
	GETCONFIGVALUE("FIELD_UID",	"LDAP", _ldap_cfg.field_uid);
	GETCONFIGVALUE("FIELD_CID",	"LDAP", _ldap_cfg.field_cid);
	GETCONFIGVALUE("MIN_CID",	"LDAP", _ldap_cfg.min_cid);
	GETCONFIGVALUE("MAX_CID",	"LDAP", _ldap_cfg.max_cid);
	GETCONFIGVALUE("FIELD_NID",	"LDAP", _ldap_cfg.field_nid);
	GETCONFIGVALUE("MIN_NID",	"LDAP", _ldap_cfg.min_nid);
	GETCONFIGVALUE("MAX_NID",	"LDAP", _ldap_cfg.max_nid);
	GETCONFIGVALUE("FIELD_MAIL",	"LDAP", _ldap_cfg.field_mail);
	GETCONFIGVALUE("FIELD_QUOTA",	"LDAP", _ldap_cfg.field_maxmail);
	GETCONFIGVALUE("FIELD_PASSWD",	"LDAP", _ldap_cfg.field_passwd);
	GETCONFIGVALUE("FIELD_FWDTARGET",	"LDAP", _ldap_cfg.field_fwdtarget);
	GETCONFIGVALUE("SCOPE",		"LDAP", _ldap_cfg.scope);

	/* Store the port as an integer for later use. */
	_ldap_cfg.port_int = atoi(_ldap_cfg.port);

	/* Compare the input string with the possible options,
	 * making sure not to exceeed the length of the given string */
	{
		int len = (strlen(_ldap_cfg.scope) < 3 ? strlen(_ldap_cfg.scope) : 3);

		if (strncasecmp(_ldap_cfg.scope, "one", len) == 0)
			_ldap_cfg.scope_int = LDAP_SCOPE_ONELEVEL;
		else if (strncasecmp(_ldap_cfg.scope, "bas", len) == 0)
			_ldap_cfg.scope_int = LDAP_SCOPE_BASE;
		else if (strncasecmp(_ldap_cfg.scope, "sub", len) == 0)
			_ldap_cfg.scope_int = LDAP_SCOPE_SUBTREE;
		else
			_ldap_cfg.scope_int = LDAP_SCOPE_SUBTREE;
	}
	trace(TRACE_DEBUG,
	      "%s,%s: integer ldap scope is [%d]",__FILE__,__func__,
	      _ldap_cfg.scope_int);
}

/*
 * auth_connect()
 *
 * initializes the connection for authentication.
 * 
 * returns 0 on success, -1 on failure
 */
int auth_connect(void)
{
	__auth_get_config();
	return 0;
}

int auth_disconnect(void)
{
	/* Destroy the connection */
	if (_ldap_conn != NULL) {
		trace(TRACE_DEBUG, "%s,%s: disconnecting from ldap server",__FILE__,__func__);
		ldap_unbind(_ldap_conn);
	} else {
		trace(TRACE_DEBUG, "%s,%s: was already disconnected from ldap server",__FILE__,__func__);
	}
	return 0;
}

/*
 * At the top of each function, rebind to the server
 *
 * Someday, this will be smart enough to know if the
 * connection has a problem, and only then will it
 * do the unbind->init->bind dance.
 *
 * For now, we are lazy and resource intensive! Why?
 * Because we leave the connection open to lag to death
 * at the end of each function. It's a trade off, really.
 * We could always close it at the end, but then we'd
 * never be able to recycle a connection for a flurry of
 * calls. OTOH, if the calls are always far between, we'd
 * rather just be connected strictly as needed...
 */
int auth_reconnect(void);
int auth_reconnect(void)
{
	auth_disconnect();
	/* ...and make anew! */
	trace(TRACE_DEBUG, "%s,%s: connecting to ldap server on [%s] : [%d]",
			__FILE__,__func__, 
			_ldap_cfg.hostname, 
			_ldap_cfg.port_int);
	
	_ldap_conn = ldap_init(
			_ldap_cfg.hostname, 
			_ldap_cfg.port_int);
	
	trace(TRACE_DEBUG, "%s,%s: binding to ldap server as [%s] / [xxxxxxxx]",
			__FILE__,__func__, 
			_ldap_cfg.bind_dn);
	
	/* 
	 *
	 * TODO: 
	 *
	 * support tls connects
	 *
	 */

	
	if ((_ldap_err = ldap_bind_s(_ldap_conn, 
					_ldap_cfg.bind_dn, 
					_ldap_cfg.bind_pw, 
					LDAP_AUTH_SIMPLE))) {
		trace(TRACE_ERROR, "%s,%s: ldap_bind_s failed: %s",
				__FILE__,__func__, 
				ldap_err2string(_ldap_err));
		return -1;
	}
	trace(TRACE_DEBUG, "%s,%s: successfully bound to ldap server",
			__FILE__,__func__);
	return 0;
}

void dm_ldap_freeresult(GList *entlist)
{
	GList *fldlist, *attlist;
	entlist = g_list_first(entlist);
	while (entlist) {
		fldlist = entlist->data;
		while(fldlist) {
			attlist = fldlist->data;
			g_list_foreach(attlist,(GFunc)g_free,NULL);
			g_list_free(attlist);
			fldlist = g_list_next(fldlist);
		}
		entlist = g_list_next(entlist);
	}
}

GList * dm_ldap_entlist_get_values(GList *entlist)
{
	GList *fldlist, *attlist;
	GList *values = NULL;
	gchar *tmp;
	entlist = g_list_first(entlist);
	while (entlist) {
		fldlist = g_list_first(entlist->data);
		while (fldlist) {
			attlist = g_list_first(fldlist->data);
			while (attlist) {
				tmp = (gchar *)attlist->data;
				trace(TRACE_DEBUG,"%s,%s: value [%s]",
						__FILE__, __func__,
						tmp);
				values = g_list_append_printf(values,"%s", g_strdup(tmp));
				attlist = g_list_next(attlist);
			}
			fldlist = g_list_next(fldlist);
		}
		entlist = g_list_next(entlist);
	}
	return values;
}

char *dm_ldap_get_filter(const gchar boolean, const gchar *attribute, GList *values) 
{
	/* build user filter from objectclasses */
	gchar *s;
	GString *t = g_string_new("");
	GString *q = g_string_new("");
	GList *l = NULL;

	values = g_list_first(values);
	do {
		g_string_printf(t,"%s=%s", attribute, (char *)values->data);
		l = g_list_append(l,g_strdup(t->str));
	} while ((values = g_list_next(values)));
	
	t = g_list_join(l,")(");
	g_string_printf(q,"(%c(%s))", boolean, t->str);
	s = q->str;

	g_string_free(t,FALSE);
	g_string_free(q,FALSE);
	g_list_foreach(l,(GFunc)g_free,NULL);
	g_list_free(l);

	return s;
}
	
u64_t dm_ldap_get_freeid(const gchar *attribute)
{
	/* get the first available uidNumber/gidNumber */
	u64_t id = 0, t;
	GList *ids, *entlist;
	u64_t min = 0, max = 0;
	char *attrs[2] = { (char *)attribute, NULL };
	GString *q = g_string_new("");
	GHashTable *ht;
	
	g_string_printf(q,"(%s=*)", attribute);
	entlist = __auth_get_every_match(q->str, attrs);
	
	ids = dm_ldap_entlist_get_values(entlist);
	trace(TRACE_DEBUG,"%s,%s: found [%d] matches\n", 
			__FILE__,__func__,
			g_list_length(ids));

	if (strcmp(attribute,_ldap_cfg.field_nid)==0) {
		min = strtoull(_ldap_cfg.min_nid,NULL,10);
		max = strtoull(_ldap_cfg.max_nid,NULL,10);
	} 
	if (strcmp(attribute,_ldap_cfg.field_cid)==0) {
		min = strtoull(_ldap_cfg.min_cid,NULL,10);
		max = strtoull(_ldap_cfg.max_cid,NULL,10);
	} 
	
	ht = g_hash_table_new((GHashFunc)g_int_hash,(GEqualFunc)g_int_equal);
	ids = g_list_first(ids);
	while(ids) {
		t = strtoull(ids->data,NULL,10);
		if ((t) && (t >= min) && ((max) && (t <= max))) 
			g_hash_table_insert(ht, (gpointer)(&t), (gpointer)(ids->data));
		ids = g_list_next(ids);
	}
	for (t = min; t < max; t++) {
		if (! (g_hash_table_lookup(ht, (gpointer)(&t))))
			break;
	}
	if ((t >= min) && (t <= max))
		id=t;
		
	trace(TRACE_DEBUG,"%s,%s: return free id [%llu]\n", 
			__FILE__, __func__,
			id);
	return id;
}

/*  OLD-SCHOOL:
 *
 * Each node of retlist contains a data field
 * which is a pointer to another list, "fieldlist".
 *
 * Each node of fieldlist contains a data field
 * which is a pointer to another list, "datalist".
 *
 * Each node of datalist contains a data field
 * which is a (char *) pointer to some actual data.
 *
 * Here's a visualization:
 *
 * retlist
 *  has the "rows" that matched
 *   {
 *     (struct list *)data
 *       has the fields you requested
 *       {
 *         (struct list *)data
 *           has the values for the field
 *           {
 *             (char *)data
 *             (char *)data
 *             (char *)data
 *           }
 *       }
 *   }
 *
 *  TODO: GLIB-STYLIE:
 *
 *  ghashtable *ldap_entities
 *  {
 *    gchar *dn;
 *    ghashtable *ldap_attributes {
 *      gchar *attribute;
 *      glist *values;
 *    }
 *  }
 *  
 */


/* returns the number of matches found */
GList * __auth_get_every_match(const char *q, char **retfields)
{
	LDAPMessage *ldap_res;
	LDAPMessage *ldap_msg;
	int ldap_err;
	int ldap_attrsonly = 0;
	char *dn;
	char **ldap_vals;
	char **ldap_attrs = NULL;
	char ldap_query[AUTH_QUERY_SIZE];
	int j = 0, k = 0, m = 0;
	GList *attlist,*fldlist,*entlist;
	
	attlist = fldlist = entlist = NULL;

	if (!q) {
		trace(TRACE_ERROR, "%s,%s: got NULL query",__FILE__,__func__);
		return NULL;
	}

	auth_reconnect();

	snprintf(ldap_query, AUTH_QUERY_SIZE, "%s", q);
	trace(TRACE_DEBUG, "%s,%s: search with query [%s]",__FILE__,__func__, ldap_query);
	
	if ((ldap_err = ldap_search_s(_ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, 
			ldap_query, ldap_attrs, ldap_attrsonly, &ldap_res))) {
		trace(TRACE_ERROR, "%s,%s: query failed: %s",__FILE__,__func__,
		      ldap_err2string(ldap_err));
		if (ldap_res)
			ldap_msgfree(ldap_res);
		return NULL;
	}

	if ((j = ldap_count_entries(_ldap_conn, ldap_res)) < 1) {
		trace(TRACE_DEBUG, "%s,%s: nothing found",__FILE__,__func__);
		if (ldap_res)
			ldap_msgfree(ldap_res);
		return NULL;
	}

	/* do the first entry here */
	if ((ldap_msg = ldap_first_entry(_ldap_conn, ldap_res)) == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		trace(TRACE_ERROR, "%s,%s: ldap_first_entry failed: [%s]",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		if (ldap_res)
			ldap_msgfree(ldap_res);
		return NULL;
	}

	while (ldap_msg) {
		
		dn = ldap_get_dn(_ldap_conn, ldap_msg);
		trace(TRACE_DEBUG,"%s,%s: scan results for DN: [%s]", __FILE__, __func__, dn);
		
		for (k = 0; retfields[k] != NULL; k++) {
			if (! (ldap_vals = ldap_get_values(_ldap_conn, ldap_msg, retfields[k]))) {
				ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &ldap_err);
				trace(TRACE_ERROR, "%s,%s: ldap_get_values failed: [%s] %s",
						__FILE__,__func__, 
						retfields[k], 
						ldap_err2string(ldap_err));
			} else {
				m = 0;
				while (ldap_vals[m]) { 
					trace(TRACE_DEBUG,"%s,%s: got value [%s]\n", 
							__FILE__, __func__,
							ldap_vals[m]);
					attlist = g_list_append(attlist,g_strdup(ldap_vals[m]));
					m++;
				}
			}
			fldlist = g_list_append(fldlist, attlist);
			attlist = NULL;
			
			ldap_value_free(ldap_vals);
		}
		entlist = g_list_append(entlist, fldlist);
		fldlist = NULL;

		ldap_msg = ldap_next_entry(_ldap_conn, ldap_msg);
	}

	if (ldap_res)
		ldap_msgfree(ldap_res);
	if (ldap_msg)
		ldap_msgfree(ldap_msg);

	return entlist;
}

char *__auth_get_first_match(const char *q, char **retfields)
{
	LDAPMessage *ldap_res;
	LDAPMessage *ldap_msg;
	int ldap_err;
	int ldap_attrsonly = 0;
	char *returnid = NULL;
	char *ldap_dn = NULL;
	char **ldap_vals = NULL;
	char **ldap_attrs = NULL;
	char ldap_query[AUTH_QUERY_SIZE];
	int k = 0;

	if (!q) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL query",__FILE__,__func__);
		return returnid;
	}

	auth_reconnect();

	snprintf(ldap_query, AUTH_QUERY_SIZE, "%s", q);
	trace(TRACE_DEBUG, "%s,%s: searching with query [%s]",__FILE__,__func__, ldap_query);
	ldap_err = ldap_search_s(_ldap_conn, _ldap_cfg.base_dn,
			  _ldap_cfg.scope_int, ldap_query, ldap_attrs,
			  ldap_attrsonly, &ldap_res);
	if (ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not execute query: %s",__FILE__,__func__,
		      ldap_err2string(ldap_err));
		goto endfree;
	}

	if (ldap_count_entries(_ldap_conn, ldap_res) < 1) {
		trace(TRACE_DEBUG, "%s,%s: none found",__FILE__,__func__);
		goto endfree;
	}

	ldap_msg = ldap_first_entry(_ldap_conn, ldap_res);
	if (ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_first_entry failed: %s",__FILE__,__func__,
		      ldap_err2string(ldap_err));
		goto endfree;
	}
	
	if (! (returnid = (char *) dm_malloc(LDAP_RES_SIZE))) {
		trace(TRACE_ERROR, "%s,%s: out of memory",__FILE__,__func__);
		goto endfree;
	}
	memset(returnid,'\0',LDAP_RES_SIZE);

	for (k = 0; retfields[k] != NULL; k++) {
		ldap_vals = ldap_get_values(_ldap_conn, ldap_msg, retfields[k]);
		if (0 == strcasecmp(retfields[k], "dn")) {
			ldap_dn = ldap_get_dn(_ldap_conn, ldap_msg);
			if (ldap_dn) 
				strncpy(returnid, ldap_dn, strlen(ldap_dn));
		} else {
			if (ldap_vals) 
				strncpy(returnid, ldap_vals[0],strlen(ldap_vals[0]));
		}
	}

      endfree:
	if (ldap_dn)
		ldap_memfree(ldap_dn);
	if (ldap_vals)
		ldap_value_free(ldap_vals);
	if (!((LDAPMessage *) 0 == ldap_res))
		ldap_msgfree(ldap_res);

	trace(TRACE_DEBUG,"%s,%s: returnid [%s]", __FILE__,__func__,returnid);
	return returnid;
}


int auth_user_exists(const char *username, u64_t * user_idnr)
{
	char *id_char;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_nid, NULL };

	assert(user_idnr != NULL);
	*user_idnr = 0;

	if (!username) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as username",__FILE__,__func__);
		return 0;
	}

	snprintf(query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid,
		 username);
	
	id_char = __auth_get_first_match(query, fields);

	*user_idnr = (id_char) ? strtoull(id_char, NULL, 0) : 0;
	trace(TRACE_DEBUG, "%s,%s: returned value is [%llu]",__FILE__,__func__,
	      *user_idnr);

	if (id_char)
		dm_free(id_char);

	if (*user_idnr != 0)
		return 0;
	
	/* fall back to db-user for DBMAIL_DELIVERY_USERNAME */
	if (strcmp(username,DBMAIL_DELIVERY_USERNAME)==0)
		return db_user_exists(DBMAIL_DELIVERY_USERNAME, user_idnr);

	return 1;
}

/* Given a useridnr, find the account/login name
 * return 0 if not found, NULL on error
 */
char *auth_get_userid(u64_t user_idnr)
{
	char *returnid = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_uid, NULL };
	/*
	   if (!user_idnr)
	   {
	   trace(TRACE_ERROR,"%s,%s: got NULL as useridnr",__FILE__,__func__);
	   return 0;
	   }
	 */
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid,
		 user_idnr);
	returnid = __auth_get_first_match(query, fields);

	trace(TRACE_DEBUG, "%s,%s: returned value is [%s]",__FILE__,__func__,
	      returnid);

	return returnid;
}


/*
 * Get the Client ID number
 * Return 0 on successful failure
 * Return -1 on really big failures
 */
int auth_getclientid(u64_t user_idnr, u64_t * client_idnr)
{
	char *cid_char = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_cid, NULL };

	assert(client_idnr != NULL);
	*client_idnr = 0;

	if (!user_idnr) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as useridnr",__FILE__,__func__);
		return -1;
	}

	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid,
		 user_idnr);
	cid_char = __auth_get_first_match(query, fields);

	*client_idnr = (cid_char) ? strtoull(cid_char, NULL, 0) : 0;
	trace(TRACE_DEBUG, "%s,%s: found client_idnr [%llu]",__FILE__,__func__,
	      *client_idnr);

	if (cid_char)
		dm_free(cid_char);

	return 1;
}


int auth_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size)
{
	char *max_char;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_maxmail, NULL };

	assert(maxmail_size != NULL);
	*maxmail_size = 0;

	if (!user_idnr) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as useridnr",__FILE__,__func__);
		return 0;
	}

	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid,
		 user_idnr);
	max_char = __auth_get_first_match(query, fields);

	*maxmail_size = (max_char) ? strtoull(max_char, 0, 10) : 0;
	trace(TRACE_DEBUG,
	      "%s,%s: returned value is [%llu]",__FILE__,__func__,
	      *maxmail_size);

	if (max_char)
		dm_free(max_char);

	return 1;
}


/*
 * auth_getencryption()
 *
 * returns a string describing the encryption used for the passwd storage
 * for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 *
 * If the specified user does not exist an empty string will be returned.
 */
char *auth_getencryption(u64_t user_idnr UNUSED)
{
	/* ldap does not support fancy passwords */
	return g_strdup("");
}
		


/* Fills the users list with all existing users
 * return -2 on mem error, -1 on db-error, 0 on success */
GList * auth_get_known_users(void)
{
	char *query;
	char *fields[] = { _ldap_cfg.field_uid, NULL };
	GList *users;
	GList *entlist;
	
	GString *t = g_string_new(_ldap_cfg.objectclass);
	GList *l = g_string_split(t,",");
	g_string_free(t,TRUE);
	query =  dm_ldap_get_filter('&',"objectClass",l);
	
	entlist = __auth_get_every_match(query, fields);
	trace(TRACE_ERROR, "%s,%s: found %d users",
			__FILE__,__func__, 
			g_list_length(entlist));

	users = dm_ldap_entlist_get_values(entlist);
	
	dm_ldap_freeresult(entlist);
	return users;
}

/*
 * auth_check_user_ext()
 * 
 * As auth_check_user() but adds the numeric ID of the user found
 * to userids or the forward to the fwds.
 * 
 * returns the number of occurences. 
 */


	
int auth_check_user_ext(const char *address, struct list *userids,
			struct list *fwds, int checks)
{
	int occurences = 0;
	u64_t id;
	char *endptr = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_nid, _ldap_cfg.field_fwdtarget, NULL };
	unsigned c2;
	char *attrvalue;
	GList *entlist, *fldlist, *attlist;

	trace(TRACE_DEBUG,
	      "%s,%s: checking user [%s] in alias table",__FILE__,__func__,
	      address);

	/* This is my private line for sending a DN rather than a search */
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_mail, address);
	entlist = __auth_get_every_match(query, fields);

	trace(TRACE_DEBUG, "%s,%s: searching with query [%s], checks [%d]",
			__FILE__,__func__, query, checks);

	if (g_list_length(entlist) < 1) {
		if (checks > 0) {
			/* found the last one, this is the deliver to
			 * but checks needs to be bigger then 0 because
			 * else it could be the first query failure */

			id = strtoull(address, &endptr, 10);
			if (*endptr == 0) {
				/* numeric deliver-to --> this is a userid */
				trace(TRACE_DEBUG, "%s,%s: adding [%llu] to userids",__FILE__,__func__, id);
				list_nodeadd(userids, &id, sizeof(id));
			} else {
				trace(TRACE_DEBUG, "%s,%s: adding [%s] to forwards",__FILE__,__func__, address);
				list_nodeadd(fwds, address, strlen(address) + 1);
				dm_free(endptr);
			}
			dm_ldap_freeresult(entlist);
			return 1;
		} else {
			trace(TRACE_DEBUG, "%s,%s: user [%s] not in aliases table",__FILE__,__func__, address);
			dm_ldap_freeresult(entlist);
			return 0;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: into checking loop",__FILE__,__func__);
	entlist = g_list_first(entlist);
	while (entlist) {
		fldlist = g_list_first(entlist->data);
		for (c2 = 0; c2 < g_list_length(fldlist); c2++) {
			attlist = g_list_first(fldlist->data);
			while(attlist) {
				attrvalue = (char *)attlist->data;
				if ((strcmp(fields[c2],_ldap_cfg.field_nid)==0)) {
					trace(TRACE_DEBUG, "%s,%s: restart with user_idnr [%s]",
							__FILE__,__func__, 
							attrvalue);
					
					occurences += auth_check_user_ext(attrvalue, userids, fwds, 1);
				} 
				
				if ((strcmp(fields[c2],_ldap_cfg.field_fwdtarget)==0)) {
					trace(TRACE_DEBUG, "%s,%s: add forwarding target [%s]",
							__FILE__,__func__, 
							attrvalue);
					
					list_nodeadd(fwds, attrvalue, strlen(attrvalue) + 1);
					occurences += 1;
				}
				
				attlist = g_list_next(attlist);
			}
			fldlist = g_list_next(fldlist);
		}
		entlist = g_list_next(entlist);
	}
	dm_ldap_freeresult(entlist);

	trace(TRACE_DEBUG, "%s,%s: executing query, checks [%d]",__FILE__,__func__, checks);

	return occurences;
}


/* 
 * auth_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX.. 
 * \bug This does not seem to work.. It should. This makes
 * this function effectively non-functional! 
 * returns a 1 on succes, -1 on failure 
 */
int auth_adduser(const char *username, const char *password, 
		 const char *enctype UNUSED, u64_t clientid, 
		 u64_t maxmail, u64_t * user_idnr)
{
	int i, j;
	/*int ret; unused variable */
	int NUM_MODS = 9;
	GString *nid = g_string_new("");
	GString *cid = g_string_new("");
	GString *maxm = g_string_new("");

	g_string_printf(nid,"%llu",dm_ldap_get_freeid(_ldap_cfg.field_nid));
	g_string_printf(cid,"%llu",clientid);
	g_string_printf(maxm,"%llu",maxmail);
	
	char **obj_values = g_strsplit(_ldap_cfg.objectclass,",",0);
	char *pw_values[] = { (char *)password, NULL };
	char *uid_values[] = { (char *)username, NULL };
	char *nid_values[] = { nid->str, NULL };
	char *cid_values[] = { cid->str, NULL };
	char *max_values[] = { maxm->str, NULL };
	
	field_t mail_type = "mail";
	field_t obj_type = "objectClass";
	unsigned _ldap_dn_len;
	
	GString *t=g_string_new("");
	
	assert(user_idnr != NULL);
	*user_idnr = 0;
	auth_reconnect();

	/* Make the malloc for all of the pieces we're about to to sprintf into it */

	g_string_printf(t,"%s=%s,%s", _ldap_cfg.cn_string, username, _ldap_cfg.base_dn);
	_ldap_dn=g_strdup(t->str);
	_ldap_dn_len=t->len;
	g_string_free(t,FALSE);
	
	trace(TRACE_DEBUG, "%s,%s: Adding user with DN of [%s]", __FILE__, __func__, _ldap_dn);

	/* Construct the array of LDAPMod structures representing the attributes 
	 * of the new entry. There's a 12 byte leak here, better find it... */

	_ldap_mod = (LDAPMod **) dm_malloc((NUM_MODS + 1) * sizeof(LDAPMod *));

	if (_ldap_mod == NULL) {
		trace(TRACE_ERROR, "%s,%s: Cannot allocate memory for mods array", __FILE__, __func__);
		return -1;
	}

	for (i = 0; i < NUM_MODS; i++) {
		if ((_ldap_mod[i] = (LDAPMod *) dm_malloc(sizeof(LDAPMod))) == NULL) {
			trace(TRACE_ERROR,
			      "%s,%s: Cannot allocate memory for mods element %d", __FILE__, __func__,
			      i);
			/* Free everything that did get allocated, which is (i-1) elements */
			for (j = 0; j < (i - 1); j++)
				dm_free(_ldap_mod[j]);
			dm_free(_ldap_mod);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	i = 0;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      "objectclass", g_strjoinv(",",obj_values));
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = obj_type;
	_ldap_mod[i]->mod_values = obj_values;
	
	if (strlen(_ldap_cfg.field_passwd) > 0) {
		i++;
		trace(TRACE_DEBUG,
		      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__,
		      i, _ldap_cfg.field_passwd, pw_values[0]);
		_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
		_ldap_mod[i]->mod_type = _ldap_cfg.field_passwd;
		_ldap_mod[i]->mod_values = pw_values;
	}

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      "mail", uid_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = mail_type;
	_ldap_mod[i]->mod_values = uid_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_uid, uid_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_uid;
	_ldap_mod[i]->mod_values = uid_values;
	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_cid, cid_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_cid;
	_ldap_mod[i]->mod_values = cid_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_maxmail, max_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_maxmail;
	_ldap_mod[i]->mod_values = max_values;

	/* FIXME: need to quackulate a free numeric user id number */
	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_nid, nid_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_ADD;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_nid;
	_ldap_mod[i]->mod_values = nid_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Placing a NULL to terminate the LDAPMod array at element %d", __FILE__, __func__,
	      i);
	_ldap_mod[i] = NULL;

	trace(TRACE_DEBUG,
	      "%s,%s: calling ldap_add_s( _ldap_conn, _ldap_dn, _ldap_mod )",__FILE__,__func__);
	_ldap_err = ldap_add_s(_ldap_conn, _ldap_dn, _ldap_mod);

	/* make sure to free this stuff even if we do bomb out! */
	/* there's a 12 byte leak here, but I can't figure out how to fix it :-( */
	g_strfreev(obj_values);

	for (i = 0; i < NUM_MODS; i++)
		dm_free(_ldap_mod[i]);
	dm_free(_ldap_mod);

/*  this function should clear the leak, but it segfaults instead :-\ */
/*  ldap_mods_free( _ldap_mod, 1 ); */
	ldap_memfree(_ldap_dn);

	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not add user: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return -1;
	}

	*user_idnr = strtoull(nid_values[0], 0, 0);
	return 1;
}


int auth_delete_user(const char *username)
{
	auth_reconnect();

	/* look up who's got that username, get their dn, and delete it! */
	if (!username) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as useridnr",__FILE__,__func__);
		return 0;
	}

	snprintf(_ldap_query, AUTH_QUERY_SIZE, "(%s=%s)",
		 _ldap_cfg.field_uid, username);
	trace(TRACE_DEBUG, "%s,%s: searching with query [%s]",__FILE__,__func__,
	      _ldap_query);
	_ldap_err =
	    ldap_search_s(_ldap_conn, _ldap_cfg.base_dn,
			  _ldap_cfg.scope_int, _ldap_query, _ldap_attrs,
			  _ldap_attrsonly, &_ldap_res);
	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not execute query: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return -1;
	}

	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		trace(TRACE_DEBUG, "%s,%s: no entries found",__FILE__,__func__);
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res);
	if (_ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&_ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_first_entry failed: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return -1;
	}

	_ldap_dn = ldap_get_dn(_ldap_conn, _ldap_msg);

	if (_ldap_dn) {
		trace(TRACE_DEBUG,
		      "%s,%s: deleting user at dn [%s]",__FILE__,__func__,
		      _ldap_dn);
		_ldap_err = ldap_delete_s(_ldap_conn, _ldap_dn);
		if (_ldap_err) {
			trace(TRACE_ERROR,
			      "%s,%s: could not delete dn: %s",__FILE__,__func__,
			      ldap_err2string(_ldap_err));
			ldap_memfree(_ldap_dn);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	ldap_memfree(_ldap_dn);
	ldap_msgfree(_ldap_res);

	return 0;
}

int auth_change_username(u64_t user_idnr, const char *new_name)
{
	int i, j, NUM_MODS = 2;
	char *new_name_str;
	char *new_values[2];

	new_name_str =
	    (char *) dm_malloc(sizeof(char) * (strlen(new_name) + 1));
	strncpy(new_name_str, new_name, strlen(new_name));

	new_values[0] = new_name_str;
	new_values[1] = NULL;

	auth_reconnect();

	if (!user_idnr) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as useridnr",__FILE__,__func__);
		return 0;
	}

	if (!new_name) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as new_name",__FILE__,__func__);
		return 0;
	}

	snprintf(_ldap_query, AUTH_QUERY_SIZE, "(%s=%llu)",
		 _ldap_cfg.field_nid, user_idnr);
	trace(TRACE_DEBUG,
	      "%s,%s: searching with query [%s]",__FILE__,__func__,
	      _ldap_query);
	_ldap_err =
	    ldap_search_s(_ldap_conn, _ldap_cfg.base_dn,
			  _ldap_cfg.scope_int, _ldap_query, _ldap_attrs,
			  _ldap_attrsonly, &_ldap_res);
	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not execute query: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return 0;
	}

	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		trace(TRACE_DEBUG,
		      "%s,%s: no entries found",__FILE__,__func__);
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res);
	if (_ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&_ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_first_entry failed: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_dn = ldap_get_dn(_ldap_conn, _ldap_msg);
	if (_ldap_dn == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&_ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_get_dn failed: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return -1;
	}
	trace(TRACE_DEBUG,
	      "%s,%s: found something at [%s]",__FILE__,__func__, _ldap_dn);

	/* Construct the array of LDAPMod structures representing the attributes 
	 * of the new entry. */

	_ldap_mod =
	    (LDAPMod **) dm_malloc((NUM_MODS + 1) * sizeof(LDAPMod *));

	if (_ldap_mod == NULL) {
		trace(TRACE_ERROR,
		      "%s,%s: Cannot allocate memory for mods array", __FILE__, __func__);
		ldap_memfree(_ldap_dn);
		ldap_msgfree(_ldap_res);
		return -1;
	}

	for (i = 0; i < NUM_MODS; i++) {
		if ((_ldap_mod[i] =
		     (LDAPMod *) dm_malloc(sizeof(LDAPMod))) == NULL) {
			trace(TRACE_ERROR,
			      "%s,%s: Cannot allocate memory for mods element %d", __FILE__, __func__,
			      i);
			/* Free everything that did get allocated, which is (i-1) elements */
			for (j = 0; j < (i - 1); j++)
				dm_free(_ldap_mod[j]);
			dm_free(_ldap_mod);
			ldap_memfree(_ldap_dn);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	i = 0;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_uid, new_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_uid;
	_ldap_mod[i]->mod_values = new_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Placing a NULL to terminate the LDAPMod array at element %d", __FILE__, __func__,
	      i);
	_ldap_mod[i] = NULL;

	trace(TRACE_DEBUG,
	      "%s,%s: calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )",__FILE__,__func__);
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, _ldap_mod);

	/* make sure to free this stuff even if we do bomb out! */
	for (i = 0; i < NUM_MODS; i++)
		dm_free(_ldap_mod[i]);
	dm_free(_ldap_mod);

	ldap_memfree(_ldap_dn);
	ldap_msgfree(_ldap_res);

	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change username: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return -1;
	}

	return 1;
}


int auth_change_password(u64_t user_idnr UNUSED,
			 const char *new_pass UNUSED,
			 const char *enctype UNUSED)
{

	return -1;
}

char * dm_ldap_user_getdn(u64_t user_idnr) {
	GString *t = g_string_new("");
	char *dn;
	
	g_string_printf(t, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr);
	trace(TRACE_DEBUG, "%s,%s: searching with query [%s]", 
			__FILE__,__func__, 
			t->str);
	
	_ldap_err = ldap_search_s(_ldap_conn, 
			_ldap_cfg.base_dn, 
			_ldap_cfg.scope_int, 
			t->str,
			_ldap_attrs, 
			_ldap_attrsonly, 
			&_ldap_res);
	
	if (_ldap_err) {
		trace(TRACE_ERROR, "%s,%s: could not execute query: %s",
				__FILE__,__func__,
				ldap_err2string(_ldap_err));
		g_string_free(t,TRUE);
		return NULL;
	}

	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		trace(TRACE_DEBUG, "%s,%s: no entries found",__FILE__,__func__);
		g_string_free(t,TRUE);
		ldap_msgfree(_ldap_res);
		return NULL;
	}

	if (! (_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res))) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		trace(TRACE_ERROR, "%s,%s: ldap_first_entry failed: %s",
				__FILE__,__func__, 
				ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return NULL;
	}

	if (! (dn = ldap_get_dn(_ldap_conn, _ldap_msg))) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		trace(TRACE_ERROR, "%s,%s: ldap_get_dn failed: %s",
				__FILE__,__func__,
				ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return NULL;
	}
	return dn;
}



int auth_change_clientid(u64_t user_idnr, u64_t newcid)
{
	int i, j, NUM_MODS = 2;
	char newcid_str[100];
	char *new_values[] = { newcid_str, NULL };

	auth_reconnect();

	if (!user_idnr) 
		return 0;
	if (!newcid)
		return 0;

	snprintf(new_values[0], 100, "%llu", newcid);

	if (! (_ldap_dn = dm_ldap_user_getdn(user_idnr)))
		return -1;
	
	if (! (_ldap_mod = (LDAPMod **) dm_malloc((NUM_MODS + 1) * sizeof(LDAPMod *)))) {
		ldap_memfree(_ldap_dn);
		ldap_msgfree(_ldap_res);
		return -1;
	}

	for (i = 0; i < NUM_MODS; i++) {
		if ((_ldap_mod[i] = (LDAPMod *) dm_malloc(sizeof(LDAPMod))) == NULL) {
			trace(TRACE_ERROR, "%s,%s: Cannot allocate memory for mods element %d", 
					__FILE__, __func__,
					i);
			/* Free everything that did get allocated, which is (i-1) elements */
			for (j = 0; j < (i - 1); j++)
				dm_free(_ldap_mod[j]);
			dm_free(_ldap_mod);
			ldap_memfree(_ldap_dn);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	i = 0;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_cid, new_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_cid;
	_ldap_mod[i]->mod_values = new_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Placing a NULL to terminate the LDAPMod array at element %d", __FILE__, __func__,
	      i);
	_ldap_mod[i] = NULL;

	trace(TRACE_DEBUG,
	      "%s,%s: calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )",__FILE__,__func__);
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, _ldap_mod);

	/* make sure to free this stuff even if we do bomb out! */
	for (i = 0; i < NUM_MODS; i++)
		dm_free(_ldap_mod[i]);
	dm_free(_ldap_mod);

	ldap_memfree(_ldap_dn);
	ldap_msgfree(_ldap_res);

	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change clientid: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return -1;
	}

	return 1;
}

int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
	int i, j, NUM_MODS = 2;
	char newsize_str[100];
	char *new_values[] = { newsize_str, NULL };

	auth_reconnect();

	if (!user_idnr) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as useridnr",__FILE__,__func__);
		return 0;
	}

	if (!new_size) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as newsize",__FILE__,__func__);
		return 0;
	}

	snprintf(new_values[0], 100, "%llu", new_size);

	snprintf(_ldap_query, AUTH_QUERY_SIZE, "(%s=%llu)",
		 _ldap_cfg.field_nid, user_idnr);
	trace(TRACE_DEBUG,
	      "%s,%s: searching with query [%s]",__FILE__,__func__,
	      _ldap_query);
	_ldap_err =
	    ldap_search_s(_ldap_conn, _ldap_cfg.base_dn,
			  _ldap_cfg.scope_int, _ldap_query, _ldap_attrs,
			  _ldap_attrsonly, &_ldap_res);
	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not execute query: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return 0;
	}

	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		trace(TRACE_DEBUG,
		      "%s,%s: no entries found",__FILE__,__func__);
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res);
	if (_ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&_ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_first_entry failed: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_dn = ldap_get_dn(_ldap_conn, _ldap_msg);
	if (_ldap_dn == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER,
				&_ldap_err);
		trace(TRACE_ERROR,
		      "%s,%s: ldap_get_dn failed: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return -1;
	}
	trace(TRACE_DEBUG,
	      "%s,%s: found something at [%s]",__FILE__,__func__,
	      _ldap_dn);

	/* Construct the array of LDAPMod structures representing the attributes 
	 * of the new entry. */

	_ldap_mod =
	    (LDAPMod **) dm_malloc((NUM_MODS + 1) * sizeof(LDAPMod *));

	if (_ldap_mod == NULL) {
		trace(TRACE_ERROR,
		      "%s,%s: Cannot allocate memory for mods array", __FILE__, __func__);
		ldap_memfree(_ldap_dn);
		ldap_msgfree(_ldap_res);
		return -1;
	}

	for (i = 0; i < NUM_MODS; i++) {
		if ((_ldap_mod[i] =
		     (LDAPMod *) dm_malloc(sizeof(LDAPMod))) == NULL) {
			trace(TRACE_ERROR,
			      "%s,%s: Cannot allocate memory for mods element %d", __FILE__, __func__,
			      i);
			/* Free everything that did get allocated, which is (i-1) elements */
			for (j = 0; j < (i - 1); j++)
				dm_free(_ldap_mod[j]);
			dm_free(_ldap_mod);
			ldap_memfree(_ldap_dn);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	i = 0;
	trace(TRACE_DEBUG,
	      "%s,%s: Starting to define LDAPMod element %d type %s value %s", __FILE__, __func__, i,
	      _ldap_cfg.field_maxmail, new_values[0]);
	_ldap_mod[i]->mod_op = LDAP_MOD_REPLACE;
	_ldap_mod[i]->mod_type = _ldap_cfg.field_maxmail;
	_ldap_mod[i]->mod_values = new_values;

	i++;
	trace(TRACE_DEBUG,
	      "%s,%s: Placing a NULL to terminate the LDAPMod array at element %d", __FILE__, __func__,
	      i);
	_ldap_mod[i] = NULL;

	trace(TRACE_DEBUG,
	      "%s,%s: calling ldap_modify_s( _ldap_conn, _ldap_dn, _ldap_mod )",__FILE__,__func__);
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, _ldap_mod);

	/* make sure to free this stuff even if we do bomb out! */
	for (i = 0; i < NUM_MODS; i++)
		dm_free(_ldap_mod[i]);
	dm_free(_ldap_mod);

	ldap_memfree(_ldap_dn);
	ldap_msgfree(_ldap_res);

	if (_ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change mailboxsize: %s",__FILE__,__func__,
		      ldap_err2string(_ldap_err));
		return -1;
	}

	return 1;
}


/* 
 * auth_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
int auth_validate(char *username, char *password, u64_t * user_idnr)
{
	timestring_t timestring;

	int ldap_err;
	char *ldap_dn = NULL;
	char *id_char = NULL;
	char query[AUTH_QUERY_SIZE];
	/*char *fields[] = { "dn", _ldap_cfg.field_nid, NULL }; unused variable */

	assert(user_idnr != NULL);

	if (username == NULL || password == NULL) {
		trace(TRACE_DEBUG, "%s,%s: username or password is NULL",__FILE__,__func__);
		return 0;
	}

	*user_idnr = 0;
	create_current_timestring(&timestring);
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid,
		 username);

	/* now, try to rebind as the given DN using the supplied password */
	trace(TRACE_ERROR,
	      "%s,%s: rebinding as [%s] to validate password",__FILE__,__func__,
	      ldap_dn);

	ldap_err =
	    ldap_bind_s(_ldap_conn, ldap_dn, password, LDAP_AUTH_SIMPLE);

	// FIXME: do we need to bind back to the dbmail "superuser" again?
	// FIXME: not at the moment because the db_reconnect() will do it for us
	if (ldap_err) {
		trace(TRACE_ERROR,
		      "%s,%s: ldap_bind_s failed: %s",__FILE__,__func__,
		      ldap_err2string(ldap_err));
		user_idnr = 0;
	} else {
		*user_idnr = (id_char) ? strtoull(id_char, NULL, 10) : 0;
		trace(TRACE_ERROR,
		      "%s,%s: return value is [%llu]",__FILE__,__func__,
		      *user_idnr);

		/* FIXME: implement this in LDAP...  log login in the database
		   snprintf(__auth_query_data, AUTH_QUERY_SIZE, "UPDATE users SET last_login = '%s' "
		   "WHERE user_idnr = '%llu'", timestring, id);

		   if (__auth_query(__auth_query_data)==-1)
		   trace(TRACE_ERROR, "%s,%s: could not update user login time",__FILE__,__func__);
		 */
	}

	if (id_char)
		dm_free(id_char);
	if (ldap_dn)
		ldap_memfree(ldap_dn);

	if (*user_idnr == 0)
		return 0;
	else
		return 1;
}

/* returns useridnr on OK, 0 on validation failed, -1 on error */
u64_t auth_md5_validate(char *username UNUSED,
			unsigned char *md5_apop_he UNUSED,
			char *apop_stamp UNUSED)
{

	return 0;
}

/**
 * \brief get user ids belonging to a client id
 * \param client_id 
 * \param user_ids
 * \param num_users
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      -  1 on success
 */
int auth_get_users_from_clientid(u64_t client_id, 
			       /*@out@*/ u64_t ** user_ids,
			       /*@out@*/ unsigned *num_users)
{

	return 1;
}

/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 * \attention aliases list needs to be empty. Method calls list_init()
 *            which sets list->start to NULL.
 */



GList * auth_get_user_aliases(u64_t user_idnr)
{
	char *fields[] = { _ldap_cfg.field_mail, NULL };
	GString *t = g_string_new("");
	GList *aliases = NULL;
	GList *entlist, *fldlist, *attlist;
	
	g_string_printf(t,"%s=%llu", _ldap_cfg.field_nid, user_idnr);
	if ((entlist = __auth_get_every_match(t->str, fields))) {
		entlist = g_list_first(entlist);
		fldlist = g_list_first(entlist->data);
		attlist = g_list_first(fldlist->data);
		while (attlist) {
			aliases = g_list_append(aliases, g_strdup(attlist->data));
			attlist = g_list_next(attlist);
		}
		dm_ldap_freeresult(entlist);
	}
	g_string_free(t,TRUE);
	return aliases;
}


/**
 * \brief add an alias for a user
 * \param user_idnr user's id
 * \param alias new alias
 * \param clientid client id
 * \return 
 *        - -1 on failure
 *        -  0 on success
 *        -  1 if alias already exists for given user
 */
int auth_addalias(u64_t user_idnr, const char *alias, u64_t clientid UNUSED)
{
	char *userid = NULL;
	char **mailValues = NULL;
	LDAPMod *modify[2], addMail;
	GList *aliases;
	int modNumber = 1;

	if (! (userid = auth_get_userid(user_idnr)))
		return -1;
	
	/* check the alias newval against the known aliases for this user */
	aliases = auth_get_user_aliases(user_idnr);
	aliases = g_list_first(aliases);
	while (aliases) {
		if (strcmp(alias,(char *)aliases->data)==0) {
			g_list_foreach(aliases,(GFunc)g_free,NULL);
			g_list_free(aliases);
			return 1;
		}
		aliases = g_list_next(aliases);
	}
	g_list_foreach(aliases,(GFunc)g_free,NULL);
	g_list_free(aliases);

	/* get the DN for this user */
	if (! (_ldap_dn = dm_ldap_user_getdn(user_idnr)))
		return -1;

	/* construct and apply the changes */
	mailValues = g_strsplit(alias,",",1);
	
	addMail.mod_op = LDAP_MOD_ADD;
	addMail.mod_type = _ldap_cfg.field_mail;
	addMail.mod_values = mailValues;
	
	modify[0] = &addMail;
	modify[1] = NULL;
			
	
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, modify);
	if (_ldap_err) {
		trace(TRACE_ERROR, "%s,%s: update failed: %s",
				__FILE__,__func__, 
				ldap_err2string(_ldap_err));
		g_strfreev(mailValues);
		ldap_memfree(_ldap_dn);
		return -1;
	}
	g_strfreev(mailValues);
	ldap_memfree(_ldap_dn);
	
	return 0;
}


/**
 * \brief add an alias to deliver to an extern address
 * \param alias the alias
 * \param deliver_to extern address to deliver to
 * \param clientid client idnr
 * \return 
 *        - -1 on failure
 *        - 0 on success
 *        - 1 if deliver_to already exists for given alias
 */
int auth_addalias_ext(const char *alias, const char *deliver_to,
		    u64_t clientid)
{
	return 0;
}


/**
 * \brief remove alias for user
 * \param user_idnr user id
 * \param alias the alias
 * \return
 *         - -1 on failure
 *         - 0 on success
 */
int auth_removealias(u64_t user_idnr, const char *alias)
{
	char *userid = NULL;
	char **mailValues = NULL;
	LDAPMod *modify[2], delMail;
	GList *aliases;
	int modNumber = 1;

	if (! (userid = auth_get_userid(user_idnr)))
		return -1;
	
	/* check the alias against the known aliases for this user */
	aliases = auth_get_user_aliases(user_idnr);
	aliases = g_list_first(aliases);
	while (aliases) {
		if (strcmp(alias,(char *)aliases->data)==0)
			break;
		
		aliases = g_list_next(aliases);
		
	}
	if (!aliases) {
		trace(TRACE_DEBUG,"%s,%s: alias [%s] for user [%s] not found",
				__FILE__, __func__,
				alias, userid);
				
		g_list_foreach(aliases,(GFunc)g_free,NULL);
		g_list_free(aliases);
		return 1;
	}
	
	g_list_foreach(aliases,(GFunc)g_free,NULL);
	g_list_free(aliases);

	/* get the DN for this user */
	if (! (_ldap_dn = dm_ldap_user_getdn(user_idnr)))
		return -1;

	/* construct and apply the changes */
	mailValues = g_strsplit(alias,",",1);
	
	delMail.mod_op = LDAP_MOD_DELETE;
	delMail.mod_type = _ldap_cfg.field_mail;
	delMail.mod_values = mailValues;
	
	modify[0] = &delMail;
	modify[1] = NULL;
			
	
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, modify);
	if (_ldap_err) {
		trace(TRACE_ERROR, "%s,%s: update failed: %s",
				__FILE__,__func__, 
				ldap_err2string(_ldap_err));
		g_strfreev(mailValues);
		ldap_memfree(_ldap_dn);
		return -1;
	}
	g_strfreev(mailValues);
	ldap_memfree(_ldap_dn);
	
	return 0;
}

/**
 * \brief remove external delivery address for an alias
 * \param alias the alias
 * \param deliver_to the deliver to address the alias is
 *        pointing to now
 * \return
 *        - -1 on failure
 *        - 0 on success
 */
int auth_removealias_ext(const char *alias, const char *deliver_to)
{
	return 0;
}


