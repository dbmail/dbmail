/*
 Copyright (c) 2002 Aaron Stone, aaron@serendipity.cx
 Copyright (c) 2005 Paul Stevens, paul@nfg.nl

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

/* User authentication functions for LDAP. */

#include "dbmail.h"
#define THIS_MODULE "auth"

#define AUTH_QUERY_SIZE 1024
#define LDAP_RES_SIZE 1024

extern char *configFile;

static LDAP *_ldap_conn = NULL;
LDAPMessage *_ldap_res;
LDAPMessage *_ldap_msg;
int _ldap_err;
int _ldap_attrsonly = 0;
char *_ldap_dn;
char **_ldap_vals;
char **_ldap_attrs = NULL;
char _ldap_query[AUTH_QUERY_SIZE];

typedef struct _ldap_cfg {
	field_t bind_dn, bind_pw, base_dn, port, uri, version, scope, hostname;
	field_t user_objectclass, forw_objectclass;
	field_t cn_string;
	field_t field_uid, field_cid, min_cid, max_cid, field_nid, min_nid, max_nid;
	field_t field_mail, field_mailalt, mailaltprefix;
	field_t field_maxmail, field_passwd;
	field_t field_fwd, field_fwdsave, field_fwdtarget, fwdtargetprefix;
	field_t field_members;
	field_t referrals;
	int scope_int, port_int, version_int;
} _ldap_cfg_t;

_ldap_cfg_t _ldap_cfg;


static GList * __auth_get_every_match(const char *q, char **retfields);

static int dm_ldap_user_shadow_rename(u64_t user_idnr, const char *new_name);
static int authldap_connect(void);
static int authldap_disconnect(void);
static int authldap_reconnect(void);
static int auth_search(const gchar *query);

static void __auth_get_config(void)
{
	static int beenhere=0;
	if (beenhere)
		return;
	
	GETCONFIGVALUE("BIND_DN",		"LDAP", _ldap_cfg.bind_dn);
	GETCONFIGVALUE("BIND_PW",		"LDAP", _ldap_cfg.bind_pw);
	GETCONFIGVALUE("BASE_DN",		"LDAP", _ldap_cfg.base_dn);
	GETCONFIGVALUE("PORT",			"LDAP", _ldap_cfg.port);
	GETCONFIGVALUE("URI",			"LDAP", _ldap_cfg.uri);
	GETCONFIGVALUE("VERSION",		"LDAP", _ldap_cfg.version);
	GETCONFIGVALUE("HOSTNAME",		"LDAP", _ldap_cfg.hostname);
	GETCONFIGVALUE("USER_OBJECTCLASS",	"LDAP", _ldap_cfg.user_objectclass);
	GETCONFIGVALUE("FORW_OBJECTCLASS",	"LDAP", _ldap_cfg.forw_objectclass);
	GETCONFIGVALUE("CN_STRING",		"LDAP", _ldap_cfg.cn_string);
	GETCONFIGVALUE("FIELD_UID",		"LDAP", _ldap_cfg.field_uid);
	GETCONFIGVALUE("FIELD_CID",		"LDAP", _ldap_cfg.field_cid);
	GETCONFIGVALUE("MIN_CID",		"LDAP", _ldap_cfg.min_cid);
	GETCONFIGVALUE("MAX_CID",		"LDAP", _ldap_cfg.max_cid);
	GETCONFIGVALUE("FIELD_NID",		"LDAP", _ldap_cfg.field_nid);
	GETCONFIGVALUE("MIN_NID",		"LDAP", _ldap_cfg.min_nid);
	GETCONFIGVALUE("MAX_NID",		"LDAP", _ldap_cfg.max_nid);
	GETCONFIGVALUE("FIELD_MAIL",		"LDAP", _ldap_cfg.field_mail);
	GETCONFIGVALUE("FIELD_QUOTA",		"LDAP", _ldap_cfg.field_maxmail);
	GETCONFIGVALUE("FIELD_PASSWD",		"LDAP", _ldap_cfg.field_passwd);
	GETCONFIGVALUE("FIELD_FWDTARGET",	"LDAP", _ldap_cfg.field_fwdtarget);
	GETCONFIGVALUE("SCOPE",			"LDAP", _ldap_cfg.scope);
	GETCONFIGVALUE("REFERRALS",		"LDAP", _ldap_cfg.referrals);

	/* Store the port as an integer for later use. */
	_ldap_cfg.port_int = atoi(_ldap_cfg.port);

	/* Store the version as an integer for later use. */
	_ldap_cfg.version_int = atoi(_ldap_cfg.version);
	/* defaults to version 3 */
	if (!_ldap_cfg.version_int)
		_ldap_cfg.version_int=3;
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
	TRACE(TRACE_DEBUG, "integer ldap scope is [%d]", _ldap_cfg.scope_int);
	
	beenhere++;
}

static int auth_ldap_bind(void)
{
	TRACE(TRACE_DEBUG, "binding to ldap server as [%s] / [xxxxxxxx]",  _ldap_cfg.bind_dn);
	
	/* 
	 * TODO: support tls connects
	 */
	
	if ((_ldap_err = ldap_bind_s(_ldap_conn, 
					_ldap_cfg.bind_dn, 
					_ldap_cfg.bind_pw, 
					LDAP_AUTH_SIMPLE))) {
		TRACE(TRACE_ERROR, "ldap_bind_s failed: %s",  ldap_err2string(_ldap_err));
		return -1;
	}
	TRACE(TRACE_DEBUG, "successfully bound to ldap server" );

	return 0;

}

/* Module api wrappers */
int auth_connect(void)
	{ return authldap_connect(); }
int auth_disconnect(void)
	{ return authldap_disconnect(); }
/*
 * authldap_connect()
 *
 * initializes the connection for authentication.
 * 
 * returns 0 on success, -1 on failure
 */
static int authldap_connect(void)
{
	int version = 0;
#ifdef HAVE_LDAP_INITIALIZE
	int ret;
	char *uri;
#endif

       if (_ldap_conn != NULL)
               return 0;

	__auth_get_config();

	switch (_ldap_cfg.version_int) {
	case 3:
		version = LDAP_VERSION3;
		if (strlen(_ldap_cfg.uri)) {
#ifdef HAVE_LDAP_INITIALIZE
			TRACE(TRACE_DEBUG, 
				"connecting to ldap server on [%s] version [%d]", 
				_ldap_cfg.uri, _ldap_cfg.version_int);
			if ((ret = ldap_initialize(&_ldap_conn, _ldap_cfg.uri) 
				== LDAP_SUCCESS)) 
				break;
			else 
				TRACE(TRACE_WARNING, "ldap_initialize() returned %d", ret);
#else
			TRACE(TRACE_WARNING, "LDAP library doesn't support ldap_initialize()."
				" You cannot use the URI directive to specify the DSN.");
#endif
		}

#ifdef HAVE_LDAP_INITIALIZE
		uri = g_strdup_printf("ldap://%s:%d", _ldap_cfg.hostname, _ldap_cfg.port_int);

		TRACE(TRACE_DEBUG, 
			"connecting to ldap server on [%s] version [%d]", 
			uri, _ldap_cfg.version_int);
		if ((ret = ldap_initialize(&_ldap_conn, uri)) != LDAP_SUCCESS) 
			TRACE(TRACE_FATAL, "ldap_initialize() returned [%d]", ret);

		g_free(uri);
#else
		TRACE(TRACE_DEBUG, 
			"connecting to ldap server on [%s] : [%d] version [%d]",
			_ldap_cfg.hostname, _ldap_cfg.port_int, _ldap_cfg.version_int);
		_ldap_conn = ldap_init(_ldap_cfg.hostname, _ldap_cfg.port_int);
#endif
		break;
	case 2:
		version = LDAP_VERSION2;
		/* fall through... */
	default:
		if (!version) {
			TRACE(TRACE_WARNING, "Unsupported LDAP version [%d] requested."
				" Default to LDAP version 3.", _ldap_cfg.version_int);
			version = LDAP_VERSION3;
		}

		TRACE(TRACE_DEBUG, 
			"connecting to ldap server on [%s] : [%d] version [%d]",
			_ldap_cfg.hostname, _ldap_cfg.port_int, _ldap_cfg.version_int);
		_ldap_conn = ldap_init(_ldap_cfg.hostname, _ldap_cfg.port_int);
		break;
	}

	ldap_set_option(_ldap_conn, LDAP_OPT_PROTOCOL_VERSION, &version);

	/* Turn off referrals */
	if (strncasecmp(_ldap_cfg.referrals, "no", 2) == 0) {
		ldap_set_option(_ldap_conn, LDAP_OPT_REFERRALS, 0);
	}

	return auth_ldap_bind();	
}

static int authldap_disconnect(void) 
{
	/* Destroy the connection */
	if (_ldap_conn != NULL) {
		/* If LDAP server has gone, we will catch a SIGPIPE in 
		 * ldap_unbind... we should ignore it.  */
		struct sigaction act, oldact;

		memset(&act, 0, sizeof(act));
		memset(&oldact, 0, sizeof(oldact));
		act.sa_handler = SIG_IGN;
		sigaction(SIGPIPE, &act, &oldact);

		ldap_unbind(_ldap_conn);
		_ldap_conn = NULL;

		sigaction(SIGPIPE, &oldact, 0);
	}
	return 0;
}

static int authldap_reconnect(void)
{
	authldap_disconnect();
	return authldap_connect();
}

static int auth_search(const gchar *query)
{
	int c=0;

	g_return_val_if_fail(query!=NULL, DM_EQUERY);
	
	while (c++ < 5) {
		TRACE(TRACE_DEBUG, " [%s]", query);
		_ldap_err = ldap_search_s(_ldap_conn, _ldap_cfg.base_dn, _ldap_cfg.scope_int, 
				query, _ldap_attrs, _ldap_attrsonly, &_ldap_res);
		
		if (! _ldap_err)
			return 0;
		
		switch (_ldap_err) {
			case LDAP_SERVER_DOWN:
				TRACE(TRACE_WARNING, "LDAP gone away: %s. Try to reconnect(%d/5).", ldap_err2string(_ldap_err),c);
				if (authldap_reconnect())
					sleep(2); // reconnect failed. wait before trying again
				break;
			default:
				TRACE(TRACE_ERROR, "LDAP error(%d): %s", _ldap_err, ldap_err2string(_ldap_err));
				return _ldap_err;
				break;
		}
	}
	
	TRACE(TRACE_FATAL,"unrecoverable error while talking to ldap server");
	return -1;
}

void dm_ldap_freeresult(GList *entlist)
{
	GList *fldlist, *attlist;
	entlist = g_list_first(entlist);
	while (entlist) {
		fldlist = entlist->data;
		while(fldlist) {
			attlist = fldlist->data;
			g_list_destroy(attlist);
			if (! g_list_next(fldlist))
				break;
			fldlist = g_list_next(fldlist);
		}
		g_list_free(g_list_first(fldlist));

		if (! g_list_next(entlist))
			break;
		entlist = g_list_next(entlist);
	}

	g_list_free(g_list_first(entlist));
}

GList * dm_ldap_entdm_list_get_values(GList *entlist)
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
				TRACE(TRACE_DEBUG,"value [%s]", tmp);
				values = g_list_append_printf(values,"%s", tmp);
				if (! g_list_next(attlist))
					break;
				attlist = g_list_next(attlist);
			}
			if (! g_list_next(fldlist))
				break;
			fldlist = g_list_next(fldlist);
		}
		if (! g_list_next(entlist))
			break;
		entlist = g_list_next(entlist);
	}
	return values;
}

static char *dm_ldap_get_filter(const gchar boolean, const gchar *attribute, GList *values) 
{
	/* build user filter from objectclasses */
	gchar *s;
	GString *t = g_string_new("");
	GString *q = g_string_new("");
	GList *l = NULL;

	values = g_list_first(values);
	while (values) {
		g_string_printf(t,"%s=%s", attribute, (char *)values->data);
		l = g_list_append(l,g_strdup(t->str));
		if (! g_list_next(values))
			break;
		values = g_list_next(values);
	}
	t = g_list_join(l,")(");
	g_string_printf(q,"(%c(%s))", boolean, t->str);
	s = q->str;

	g_string_free(t,TRUE);
	g_string_free(q,FALSE);
	g_list_foreach(l,(GFunc)g_free,NULL);
	g_list_free(l);

	return s;
}
	
static u64_t dm_ldap_get_freeid(const gchar *attribute)
{
	/* get the first available uidNumber/gidNumber */
	u64_t id = 0, t;
	GList *ids, *entlist;
	u64_t min = 0, max = 0;
	char *attrs[2] = { (char *)attribute, NULL };
	GString *q = g_string_new("");
	u64_t *key;
	
	g_string_printf(q,"(%s=*)", attribute);
	entlist = __auth_get_every_match(q->str, attrs);
	
	ids = dm_ldap_entdm_list_get_values(entlist);
	
	/* get the valid range */
	if (strcmp(attribute,_ldap_cfg.field_nid)==0) {
		min = strtoull(_ldap_cfg.min_nid,NULL,10);
		max = strtoull(_ldap_cfg.max_nid,NULL,10);
	} 
	if (strcmp(attribute,_ldap_cfg.field_cid)==0) {
		min = strtoull(_ldap_cfg.min_cid,NULL,10);
		max = strtoull(_ldap_cfg.max_cid,NULL,10);
	} 
	g_assert(min < max);
	
	/* allocate the key array */
	key = g_new0(u64_t, 1 + max - min );
	
	/* get all used ids */
	ids = g_list_first(ids);
	while (ids) {
		t = strtoull(ids->data,NULL,10);
		if ( (t >= min) && (t <= max) ) 
			key[t-min] = t;
		if (! g_list_next(ids))
			break;
		ids = g_list_next(ids);
	}

	/* find the first unused id */
	for (t = min; t <= max; t++) {
		if (! (key[t-min])) 
			break;
	}

	g_assert( (t >= min) && (t <= max) );
	
	/* cleanup */
	g_free(key);
	g_list_foreach(ids,(GFunc)g_free,NULL);
	g_list_free(ids);
	
	id=t;
	TRACE(TRACE_DEBUG,"return free id [%llu]\n", id);
	return id;
}

static char * dm_ldap_user_getdn(u64_t user_idnr) 
{
	GString *t = g_string_new("");
	char *dn;
	
	g_string_printf(t, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr);
	TRACE(TRACE_DEBUG, "searching with query [%s]", t->str);
	
	
	if (auth_search(t->str)) {
		g_string_free(t,TRUE);
		return NULL;
	}
		
	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		TRACE(TRACE_DEBUG, "no entries found");
		g_string_free(t,TRUE);
		ldap_msgfree(_ldap_res);
		return NULL;
	}

	if (! (_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res))) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		TRACE(TRACE_ERROR, "ldap_first_entry failed: %s", ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return NULL;
	}

	if (! (dn = ldap_get_dn(_ldap_conn, _ldap_msg))) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		TRACE(TRACE_ERROR, "ldap_get_dn failed: %s", ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return NULL;
	}

	ldap_msgfree(_ldap_res);
	return dn;
}

static int dm_ldap_mod_field(u64_t user_idnr, const char *fieldname, const char *newvalue)
{

	LDAPMod *mods[2], modField; 
	char *newvalues[2];
	
	
	if (! user_idnr) {
		TRACE(TRACE_ERROR, "no user_idnr specified");
		return -1;
	}
	if (! fieldname) {
		TRACE(TRACE_ERROR, "no fieldname specified");
		return -1;
	}
	if (! newvalue) {
		TRACE(TRACE_ERROR, "no new value specified");
		return -1;
	}
		
	if (! (_ldap_dn = dm_ldap_user_getdn(user_idnr)))
		return -1;

	newvalues[0] = (char *)newvalue;
	newvalues[1] = NULL;

	modField.mod_op = LDAP_MOD_REPLACE;
	modField.mod_type = (char *)fieldname;
	modField.mod_values = newvalues;

	mods[0] = &modField;
	mods[1] = NULL;

	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, mods);
	if (_ldap_err) {
		TRACE(TRACE_ERROR,"dn: %s, %s: %s [%s]", _ldap_dn, fieldname, newvalue, ldap_err2string(_ldap_err));
		ldap_memfree(_ldap_dn);
		return -1;
	}
	TRACE(TRACE_DEBUG,"dn: %s, %s: %s", _ldap_dn, fieldname, newvalue);
	ldap_memfree(_ldap_dn);
	return 0;
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
 *     (struct dm_list *)data
 *       has the fields you requested
 *       {
 *         (struct dm_list *)data
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
static GList * __auth_get_every_match(const char *q, char **retfields)
{
	LDAPMessage *ldap_msg;
	char **ldap_vals = NULL;
	char *dn;
	int j = 0, k = 0, m = 0;
	GList *attlist,*fldlist,*entlist;
	
	attlist = fldlist = entlist = NULL;

	if (auth_search(q))
		return NULL;

	if ((j = ldap_count_entries(_ldap_conn, _ldap_res)) < 1) {
		TRACE(TRACE_DEBUG, "nothing found");
		if (_ldap_res)
			ldap_msgfree(_ldap_res);
		return NULL;
	}

	/* do the first entry here */
	if ((ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res)) == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		TRACE(TRACE_ERROR, "ldap_first_entry failed: [%s]", ldap_err2string(_ldap_err));
		if (_ldap_res)
			ldap_msgfree(_ldap_res);
		return NULL;
	}

	while (ldap_msg) {
		
		dn = ldap_get_dn(_ldap_conn, ldap_msg);
		TRACE(TRACE_DEBUG,"scan results for DN: [%s]", dn);
		
		for (k = 0; retfields[k] != NULL; k++) {
			TRACE(TRACE_DEBUG,"ldap_get_values [%s]", retfields[k]);
			if ((ldap_vals = ldap_get_values(_ldap_conn, ldap_msg, retfields[k]))) {
				m = 0;
				while (ldap_vals[m]) { 
					TRACE(TRACE_DEBUG,"got value [%s]", ldap_vals[m]);
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
		
		ldap_memfree(dn);
		
		ldap_msg = ldap_next_entry(_ldap_conn, ldap_msg);
	}

	if (_ldap_res)
		ldap_msgfree(_ldap_res);
	if (ldap_msg)
		ldap_msgfree(ldap_msg);

	return entlist;
}

static char *__auth_get_first_match(const char *q, char **retfields)
{
	LDAPMessage *ldap_msg;
	char *returnid = NULL;
	char *ldap_dn = NULL;
	char **ldap_vals = NULL;
	int k = 0;

	if (auth_search(q))
		return NULL;
	
	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		TRACE(TRACE_DEBUG, "none found");
		goto endfree;
	}

	ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res);
	if (ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		TRACE(TRACE_ERROR, "ldap_first_entry failed: %s", ldap_err2string(_ldap_err));
		goto endfree;
	}
	
	for (k = 0; retfields[k] != NULL; k++) {
		if (0 == strcasecmp(retfields[k], "dn")) {
			ldap_dn = ldap_get_dn(_ldap_conn, ldap_msg);
			if (ldap_dn)
				returnid = g_strdup(ldap_dn);
			break;
		} else {
			ldap_vals = ldap_get_values(_ldap_conn, ldap_msg, retfields[k]);
			if (ldap_vals) 
				returnid = g_strdup(ldap_vals[0]);
			break;
		}
	}
	
      endfree:
	if (ldap_dn)
		ldap_memfree(ldap_dn);
	if (ldap_vals)
		ldap_value_free(ldap_vals);
	if (_ldap_res)
		ldap_msgfree(_ldap_res);

	TRACE(TRACE_DEBUG,"returnid [%s]",returnid);
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
		TRACE(TRACE_ERROR, "got NULL as username");
		return 0;
	}

	/* fall back to db-user for DBMAIL_DELIVERY_USERNAME */
	if (strcmp(username,DBMAIL_DELIVERY_USERNAME)==0)
		return db_user_exists(DBMAIL_DELIVERY_USERNAME, user_idnr);
	
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid,
		 username);
	
	id_char = __auth_get_first_match(query, fields);
	*user_idnr = (id_char) ? strtoull(id_char, NULL, 0) : 0;
	if (id_char != NULL)
		g_free(id_char);

	TRACE(TRACE_DEBUG, "returned value is [%llu]", *user_idnr);

	if (*user_idnr != 0)
		return 1;

	return 0;
}

/* Given a useridnr, find the account/login name
 * return 0 if not found, NULL on error
 */
char *auth_get_userid(u64_t user_idnr)
{
	char *returnid = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_uid, NULL };
	
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr);
	returnid = __auth_get_first_match(query, fields);
	TRACE(TRACE_DEBUG, "returned value is [%s]", returnid);

	return returnid;
}

/* We'd like to have -1 return on failure, but
 * the internal ldap api here won't tell us. */
int auth_check_userid(u64_t user_idnr)
{
	char *returnid = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { _ldap_cfg.field_nid, NULL };
	int ret;
	
	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid, user_idnr);
	returnid = __auth_get_first_match(query, fields);

	if (returnid) {
		ret = 0;
		TRACE(TRACE_DEBUG, "found user_idnr [%llu]", user_idnr);
	} else {
		ret = 1;
		TRACE(TRACE_DEBUG, "didn't find user_idnr [%llu]", user_idnr);
	}

	g_free(returnid);

	return ret;
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
		TRACE(TRACE_ERROR, "got NULL as useridnr");
		return -1;
	}

	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid,
		 user_idnr);
	cid_char = __auth_get_first_match(query, fields);
	*client_idnr = (cid_char) ? strtoull(cid_char, NULL, 0) : 0;
	if (cid_char != NULL)
		g_free(cid_char);

	TRACE(TRACE_DEBUG, "found client_idnr [%llu]", *client_idnr);

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
		TRACE(TRACE_ERROR, "got NULL as useridnr");
		return 0;
	}

	snprintf(query, AUTH_QUERY_SIZE, "(%s=%llu)", _ldap_cfg.field_nid,
		 user_idnr);
	max_char = __auth_get_first_match(query, fields);
	*maxmail_size = (max_char) ? strtoull(max_char, 0, 10) : 0;
	
	// if max_char is NULL g_free will not fail it simply return
	g_free(max_char);

	TRACE(TRACE_DEBUG, "returned value is [%llu]", *maxmail_size);

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
	/* ldap does not support fancy passwords, but return 
	 * something valid for the sql shadow */
	return "md5";
}
		


/* Fills the users list with all existing users
 * return -2 on mem error, -1 on db-error, 0 on success */
GList * auth_get_known_users(void)
{
	char *query;
	char *fields[] = { _ldap_cfg.field_uid, NULL };
	GList *users;
	GList *entlist;
	
	GString *t = g_string_new(_ldap_cfg.user_objectclass);
	GList *l = g_string_split(t,",");
	g_string_free(t,TRUE);
	
	query =  dm_ldap_get_filter('&',"objectClass",l);
	entlist = __auth_get_every_match(query, fields);
	g_free(query);
	
	TRACE(TRACE_INFO, "found %d users", g_list_length(entlist));

	users = dm_ldap_entdm_list_get_values(entlist);
	
	dm_ldap_freeresult(entlist);
	return users;
}

/* Fills the aliases list with all existing aliases
 * return -2 on mem error, -1 on db-error, 0 on succeses */
GList * auth_get_known_aliases(void)
{
	char *query;
	char *fields[] = { _ldap_cfg.field_uid, NULL };
	GList *aliases;
	GList *entlist;
	
	GString *t = g_string_new(_ldap_cfg.forw_objectclass);
	GList *l = g_string_split(t,",");
	g_string_free(t,TRUE);
	
	query =  dm_ldap_get_filter('&',"objectClass",l);
	entlist = __auth_get_every_match(query, fields);
	g_free(query);
	
	TRACE(TRACE_INFO, "found %d aliases", g_list_length(entlist));

	aliases = dm_ldap_entdm_list_get_values(entlist);
	
	dm_ldap_freeresult(entlist);
	return aliases;
}

/*
 * auth_check_user_ext()
 * 
 * As auth_check_user() but adds the numeric ID of the user found
 * to userids or the forward to the fwds.
 * 
 * returns the number of occurences. 
 */


	
int auth_check_user_ext(const char *address, struct dm_list *userids,
			struct dm_list *fwds, int checks)
{
	int occurences = 0;
	u64_t id;
	char *endptr = NULL;
	char query[AUTH_QUERY_SIZE];
	char *fields[] = { 
		_ldap_cfg.field_nid, 
		_ldap_cfg.field_fwdtarget[0] ? _ldap_cfg.field_fwdtarget : NULL, 
		NULL 
	};
	char *attrvalue;
	GList *entlist, *fldlist, *attlist, *searchlist;

	if (checks > 20) {
		TRACE(TRACE_ERROR, "too many checks. Possible loop detected.");
		return 0;
	}

	TRACE(TRACE_DEBUG, "checking user [%s] in alias table", address);

	/* build a mail filter, with multiple attributes, if needed */
	GString *f = g_string_new(_ldap_cfg.field_mail);
	searchlist = g_string_split(f,",");
	g_string_free(f,TRUE);
	
	GString *t = g_string_new("");
	GString *q = g_string_new("");
	GList *l = NULL;
	searchlist = g_list_first(searchlist);
	while(searchlist) {
		g_string_printf(t,"%s=%s",(char *)searchlist->data,address);
		l = g_list_append(l,g_strdup(t->str));
		if(!g_list_next(searchlist))
			break;
		searchlist = g_list_next(searchlist);	
	}

	t = g_list_join(l,")(");
	g_string_printf(q,"(|(%s))", t->str);
	snprintf(query, AUTH_QUERY_SIZE, q->str);
	g_string_free(t,TRUE);
	g_string_free(q,FALSE);
	g_list_foreach(l,(GFunc)g_free,NULL);
	g_list_free(l);	
	
	TRACE(TRACE_DEBUG, "searching with query [%s], checks [%d]", query, checks);

	entlist = __auth_get_every_match(query, fields);

	if (g_list_length(entlist) < 1) {
		if (checks > 0) {
			/* found the last one, this is the deliver to
			 * but checks needs to be bigger then 0 because
			 * else it could be the first query failure */

			id = strtoull(address, &endptr, 10);
			if (*endptr == 0) {
				/* numeric deliver-to --> this is a userid */
				TRACE(TRACE_DEBUG, "adding [%llu] to userids", id);
				dm_list_nodeadd(userids, &id, sizeof(id));
			} else {
				TRACE(TRACE_DEBUG, "adding [%s] to forwards", address);
				dm_list_nodeadd(fwds, address, strlen(address) + 1);
			}
			return 1;
		} else {
			TRACE(TRACE_DEBUG, "user [%s] not in aliases table", address);
			dm_ldap_freeresult(entlist);
			return 0;
		}
	}

	TRACE(TRACE_DEBUG, "into checking loop");
	entlist = g_list_first(entlist);
	while (entlist) {
		fldlist = g_list_first(entlist->data);
		while(fldlist) {
			attlist = g_list_first(fldlist->data);
			while(attlist) {
				attrvalue = (char *)attlist->data;
				occurences += auth_check_user_ext(attrvalue, userids, fwds, checks+1);
				
				if (! g_list_next(attlist))
					break;
				attlist = g_list_next(attlist);
			}
			if (! g_list_next(fldlist))
				break;
			fldlist = g_list_next(fldlist);
		}
		if (! g_list_next(entlist))
			break;
		entlist = g_list_next(entlist);
	}
	dm_ldap_freeresult(entlist);
	return occurences;
}


/* 
 * auth_adduser()
 *
 * adds a new user to the database 
 * and adds a INBOX.. 
 * returns a 1 on succes, -1 on failure 
 */
int auth_adduser(const char *username, const char *password, 
		 const char *enctype UNUSED, u64_t clientid, 
		 u64_t maxmail, u64_t * user_idnr)
{
	int i = 0, result;
	GString *nid = g_string_new("");
	GString *cid = g_string_new("");
	GString *maxm = g_string_new("");
	
	u64_t newidnr = dm_ldap_get_freeid(_ldap_cfg.field_nid);

	g_string_printf(nid,"%llu", newidnr);
	g_string_printf(cid,"%llu",clientid);
	g_string_printf(maxm,"%llu",maxmail);
	
	char **obj_values = g_strsplit(_ldap_cfg.user_objectclass,",",0);
	char *pw_values[] = { (char *)password, NULL };
	char *uid_values[] = { (char *)username, NULL };
	char *nid_values[] = { nid->str, NULL };
	char *cid_values[] = { cid->str, NULL };
	char *max_values[] = { maxm->str, NULL };
	
	field_t mail_type = "mail";
	field_t obj_type = "objectClass";

	GString *t=g_string_new("");
	
	assert(user_idnr != NULL);
	*user_idnr = 0;

	g_string_printf(t,"%s=%s,%s", _ldap_cfg.cn_string, username, _ldap_cfg.base_dn);
	_ldap_dn=g_strdup(t->str);
	g_string_free(t,TRUE);
	
	TRACE(TRACE_DEBUG, "Adding user with DN of [%s]", _ldap_dn);
	
	LDAPMod *mods[10], mod_obj_type, mod_field_passwd, mod_mail_type,
	mod_field_uid, mod_field_cid, mod_field_maxmail, mod_field_nid;

	
	mod_obj_type.mod_op = LDAP_MOD_ADD;
	mod_obj_type.mod_type = obj_type;
	mod_obj_type.mod_values = obj_values;
	mods[i++] = &mod_obj_type;
	
	if (strlen(_ldap_cfg.field_passwd) > 0) {
		mod_field_passwd.mod_op = LDAP_MOD_ADD;
		mod_field_passwd.mod_type = _ldap_cfg.field_passwd;
		mod_field_passwd.mod_values = pw_values;
		mods[i++] = &mod_field_passwd;
	}

	mod_mail_type.mod_op = LDAP_MOD_ADD;
	mod_mail_type.mod_type = mail_type;
	mod_mail_type.mod_values = uid_values;
	mods[i++] = &mod_mail_type;

	mod_field_uid.mod_op = LDAP_MOD_ADD;
	mod_field_uid.mod_type = _ldap_cfg.field_uid;
	mod_field_uid.mod_values = uid_values;
	mods[i++] = &mod_field_uid;
	
	mod_field_cid.mod_op = LDAP_MOD_ADD;
	mod_field_cid.mod_type = _ldap_cfg.field_cid;
	mod_field_cid.mod_values = cid_values;
	mods[i++] = &mod_field_cid;

	mod_field_maxmail.mod_op = LDAP_MOD_ADD;
	mod_field_maxmail.mod_type = _ldap_cfg.field_maxmail;
	mod_field_maxmail.mod_values = max_values;
	mods[i++] = &mod_field_maxmail;

	mod_field_nid.mod_op = LDAP_MOD_ADD;
	mod_field_nid.mod_type = _ldap_cfg.field_nid;
	mod_field_nid.mod_values = nid_values;
	mods[i++] = &mod_field_nid;

	mods[i++] = NULL;

	_ldap_err = ldap_add_s(_ldap_conn, _ldap_dn, mods);

	g_strfreev(obj_values);
	ldap_memfree(_ldap_dn);

	if (_ldap_err) {
		TRACE(TRACE_ERROR, "could not add user: %s", ldap_err2string(_ldap_err));
		return -1;
	}

	*user_idnr = newidnr;
	result = db_user_create_shadow(username, user_idnr);
	
	if (result != 1) {
		TRACE(TRACE_ERROR, "sql shadow account creation failed");
		auth_delete_user(username);
		*user_idnr=0;
		return result;
	}
	
	
	return 1;
}


int auth_delete_user(const char *username)
{
	/* look up who's got that username, get their dn, and delete it! */
	if (!username) {
		TRACE(TRACE_ERROR, "got NULL as useridnr");
		return 0;
	}

	snprintf(_ldap_query, AUTH_QUERY_SIZE, "(%s=%s)", _ldap_cfg.field_uid, username);

	if (auth_search(_ldap_query))
		return -1;
	
	if (ldap_count_entries(_ldap_conn, _ldap_res) < 1) {
		TRACE(TRACE_DEBUG, "no entries found");
		ldap_msgfree(_ldap_res);
		return 0;
	}

	_ldap_msg = ldap_first_entry(_ldap_conn, _ldap_res);
	if (_ldap_msg == NULL) {
		ldap_get_option(_ldap_conn, LDAP_OPT_ERROR_NUMBER, &_ldap_err);
		TRACE(TRACE_ERROR, "ldap_first_entry failed: %s", ldap_err2string(_ldap_err));
		ldap_msgfree(_ldap_res);
		return -1;
	}

	_ldap_dn = ldap_get_dn(_ldap_conn, _ldap_msg);

	if (_ldap_dn) {
		TRACE(TRACE_DEBUG, "deleting user at dn [%s]", _ldap_dn);
		_ldap_err = ldap_delete_s(_ldap_conn, _ldap_dn);
		if (_ldap_err) {
			TRACE(TRACE_ERROR, "could not delete dn: %s", ldap_err2string(_ldap_err));
			ldap_memfree(_ldap_dn);
			ldap_msgfree(_ldap_res);
			return -1;
		}
	}

	ldap_memfree(_ldap_dn);
	ldap_msgfree(_ldap_res);
	
	if (db_user_delete(username)) {
		TRACE(TRACE_ERROR, "sql shadow account deletion failed");
	}
	
	return 0;
}

static int dm_ldap_user_shadow_rename(u64_t user_idnr, const char *new_name)
{
	char *oldname;
	u64_t dbidnr;
	oldname = auth_get_userid(user_idnr);
	db_user_exists(oldname,&dbidnr);
	if (dbidnr) {
		TRACE(TRACE_DEBUG, "call db_user_rename ([%llu],[%s])\n", dbidnr, new_name);
	}
	if ((! dbidnr) || (db_user_rename(dbidnr, new_name))) {
		TRACE(TRACE_ERROR, "renaming shadow account in db failed for [%llu]->[%s]", user_idnr, new_name);
		return -1;
	}
	return 0;
}

int auth_change_username(u64_t user_idnr, const char *new_name)
{
	GString *newrdn;
	
	if (!user_idnr) {
		TRACE(TRACE_ERROR, "got NULL as useridnr");
		return -1;
	}

	if (!new_name) {
		TRACE(TRACE_ERROR, "got NULL as new_name");
		return -1;
	}
	
	if (! (_ldap_dn = dm_ldap_user_getdn(user_idnr)))
		return -1;
	
	TRACE(TRACE_DEBUG, "got DN [%s]", _ldap_dn);

	
	db_begin_transaction();
	dm_ldap_user_shadow_rename(user_idnr, new_name);
	
	/* perhaps we have to rename the dn */
	if (strcmp(_ldap_cfg.field_uid,_ldap_cfg.cn_string)==0) {
		newrdn = g_string_new("");
		g_string_printf(newrdn,"%s=%s", _ldap_cfg.cn_string,new_name);
		
		_ldap_err = ldap_modrdn_s(_ldap_conn, _ldap_dn, newrdn->str);
		
		ldap_memfree(_ldap_dn);
		g_string_free(newrdn,TRUE);
		
		if (_ldap_err) {
			TRACE(TRACE_ERROR, "error calling ldap_modrdn_s [%s]", ldap_err2string(_ldap_err));
			db_rollback_transaction();
			return -1;
		}
		db_commit_transaction();
		return 0;
	}
	/* else we need to modify an attribute */
	
	ldap_memfree(_ldap_dn);
	
	if (dm_ldap_mod_field(user_idnr, _ldap_cfg.field_uid, new_name)) {
		db_rollback_transaction();
		return -1;
	}
	db_commit_transaction();
	
	return 0;
	
}

int auth_change_password(u64_t user_idnr, const char *new_pass, const char *enctype UNUSED)
{
	return dm_ldap_mod_field(user_idnr, _ldap_cfg.field_passwd, new_pass);
}


int auth_change_clientid(u64_t user_idnr, u64_t newcid)
{
	char newcid_str[16];
	snprintf(newcid_str, 16, "%llu", newcid);
	return dm_ldap_mod_field(user_idnr, _ldap_cfg.field_cid, newcid_str);
}

int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
	int result;
	char newsize_str[16];
	snprintf(newsize_str, 16, "%llu", new_size);
	if ((result = db_change_mailboxsize(user_idnr, new_size)))
		return result;
	return dm_ldap_mod_field(user_idnr, _ldap_cfg.field_maxmail, newsize_str);
}


/* 
 * auth_validate()
 *
 * tries to validate user 'user'
 *
 * returns useridnr on OK, 0 on validation failed, -1 on error 
 */
int auth_validate(clientinfo_t *ci, char *username, char *password, u64_t * user_idnr)
{
	timestring_t timestring;
	char real_username[DM_USERNAME_LEN];
	int result;
	u64_t mailbox_idnr;
	int ldap_err;
	char *ldap_dn = NULL;

	assert(user_idnr != NULL);
	*user_idnr = 0;

	if (username == NULL || password == NULL) {
		TRACE(TRACE_DEBUG, "username or password is NULL");
		return 0;
	}
	if (strlen(password) == 0) {
		TRACE(TRACE_WARNING, "User \"%s\" try to use anonimous LDAP bind!", username);
		return 0;
	}

	/* the shared mailbox user should not log in! */
	if (strcmp(username, PUBLIC_FOLDER_USER) == 0)
		return 0;

	memset(real_username,'\0', sizeof(real_username));
	
	create_current_timestring(&timestring);
	
	strncpy(real_username, username, DM_USERNAME_LEN);
	if (db_use_usermap()) {  /* use usermap */
		result = db_usermap_resolve(ci, username, real_username);
		if (result == DM_EGENERAL)
			return 0;
		if (result == DM_EQUERY)
			return DM_EQUERY;
	}

	if (auth_user_exists(real_username, user_idnr) != 1) {
		return 0;
	}
	
	if (! (ldap_dn = dm_ldap_user_getdn(*user_idnr))) {
		TRACE(TRACE_ERROR,"unable to determine DN for user");
		return 0;
	}

	/* now, try to rebind as the given DN using the supplied password */
	TRACE(TRACE_DEBUG, "rebinding as [%s] to validate password", ldap_dn);

	ldap_err = ldap_bind_s(_ldap_conn, ldap_dn, password, LDAP_AUTH_SIMPLE);

	if (ldap_err) {
		TRACE(TRACE_ERROR, "ldap_bind_s failed: %s", ldap_err2string(ldap_err));
		*user_idnr = 0;
	} else {
		db_user_log_login(*user_idnr);
	}
	
	/* rebind as admin */
	auth_ldap_bind();
	
	if (ldap_dn)
		ldap_memfree(ldap_dn);

	if (*user_idnr == 0)
		return 0;
	
	db_find_create_mailbox("INBOX", BOX_DEFAULT, *user_idnr, &mailbox_idnr);

	return 1;
}

/* returns useridnr on OK, 0 on validation failed, -1 on error */
u64_t auth_md5_validate(clientinfo_t *ci UNUSED, char *username UNUSED,
			unsigned char *md5_apop_he UNUSED,
			char *apop_stamp UNUSED)
{

	return -1;
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
int auth_get_users_from_clientid(u64_t client_id UNUSED, 
			       /*@out@*/ u64_t ** user_ids UNUSED,
			       /*@out@*/ unsigned *num_users UNUSED)
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
 * \attention aliases list needs to be empty. Method calls dm_list_init()
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
			if (! g_list_next(attlist))
				break;
			attlist = g_list_next(attlist);
		}
		dm_ldap_freeresult(entlist);
	}
	g_string_free(t,TRUE);
	return aliases;
}

/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 * \attention aliases list needs to be empty. Method calls dm_list_init()
 *            which sets list->start to NULL.
 */

GList * auth_get_aliases_ext(const char *alias)
{
	char *fields[] = { _ldap_cfg.field_fwdtarget, NULL };
	GString *t = g_string_new("");
	GList *aliases = NULL;
	GList *entlist, *fldlist, *attlist;
	
	g_string_printf(t,"%s=%s", _ldap_cfg.field_mail, alias);
	if ((entlist = __auth_get_every_match(t->str, fields))) {
		entlist = g_list_first(entlist);
		fldlist = g_list_first(entlist->data);
		attlist = g_list_first(fldlist->data);
		while (attlist) {
			aliases = g_list_append(aliases, g_strdup(attlist->data));
			if (! g_list_next(attlist))
				break;
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
		if (! g_list_next(aliases))
			break;
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
	
	g_strfreev(mailValues);
	ldap_memfree(_ldap_dn);
	
	if (_ldap_err) {
		TRACE(TRACE_ERROR, "update failed: %s", ldap_err2string(_ldap_err));
		return -1;
	}
	
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

/* find forwarding alias
 * \return
 * 	- -1 error
 * 	-  0 success
 * 	-  1 dn exists but no such deliver_to
 */

static int forward_exists(const char *alias, const char *deliver_to)
{
	char *objectfilter, *dn;
	char *fields[] = { "dn", _ldap_cfg.field_fwdtarget, NULL };
	int result = 0;
	
	GString *t = g_string_new(_ldap_cfg.forw_objectclass);
	GList *l = g_string_split(t,",");
	
	objectfilter = dm_ldap_get_filter('&',"objectClass", l);
	
	g_string_printf(t,"(&%s(%s=%s)(%s=%s))", objectfilter, _ldap_cfg.cn_string, alias, _ldap_cfg.field_fwdtarget, deliver_to);
	dn = __auth_get_first_match(t->str, fields);
	
	if (! dn) {
		result = -1; // assume total failure;
		
		g_string_printf(t,"(&%s(%s=%s))", objectfilter, _ldap_cfg.cn_string, alias);
		dn = __auth_get_first_match(t->str, fields);
		if (dn)
			result = 1; // dn does exist, just this forward is missing
		
	}
		
	
	g_free(objectfilter);
	g_free(dn);
	g_string_free(t,TRUE);
	g_list_foreach(l,(GFunc)g_free,NULL);
	
	TRACE(TRACE_DEBUG, "result [%d]", result);

	return result;
}

static int forward_create(const char *alias, const char *deliver_to)
{
	LDAPMod *mods[5], objectClass, cnField, mailField, forwField;
	
	char **obj_values = g_strsplit(_ldap_cfg.forw_objectclass,",",0);
	char *cn_values[] = { (char *)alias, NULL };
	char *mail_values[] = { (char *)alias, NULL };
	char *forw_values[] = { (char *)deliver_to, NULL };
	
	GString *t=g_string_new("");
	
	g_string_printf(t,"%s=%s,%s", _ldap_cfg.cn_string, alias, _ldap_cfg.base_dn);
	_ldap_dn=g_strdup(t->str);
	g_string_free(t,TRUE);
	
	TRACE(TRACE_DEBUG, "Adding forwardingAddress with DN of [%s]", _ldap_dn);
	
	objectClass.mod_op = LDAP_MOD_ADD;
	objectClass.mod_type = "objectClass";
	objectClass.mod_values = obj_values;
	
	cnField.mod_op = LDAP_MOD_ADD;
	cnField.mod_type = _ldap_cfg.cn_string;
	cnField.mod_values = cn_values;
	
	mailField.mod_op = LDAP_MOD_ADD;
	mailField.mod_type = _ldap_cfg.field_mail;
	mailField.mod_values = mail_values;

	forwField.mod_op = LDAP_MOD_ADD;
	forwField.mod_type = _ldap_cfg.field_fwdtarget;
	forwField.mod_values = forw_values;
	
	mods[0] = &objectClass;
	mods[1] = &cnField;
	mods[2] = &mailField;
	mods[3] = &forwField;
	mods[4] = NULL;
	
	TRACE(TRACE_DEBUG, "creating new forward [%s] -> [%s]", alias, deliver_to);
	
	_ldap_err = ldap_add_s(_ldap_conn, _ldap_dn, mods);

	g_strfreev(obj_values);
	ldap_memfree(_ldap_dn);

	if (_ldap_err) {
		TRACE(TRACE_ERROR, "could not add forwardingAddress: %s", ldap_err2string(_ldap_err));
		return -1;
	}

	return 0;

}

static int forward_add(const char *alias,const char *deliver_to) 
{
	char **mailValues = NULL;
	LDAPMod *modify[2], addForw;
	GString *t=g_string_new("");

	g_string_printf(t,"%s=%s,%s", _ldap_cfg.cn_string, alias, _ldap_cfg.base_dn);
	_ldap_dn=g_strdup(t->str);
	g_string_free(t,TRUE);

	/* construct and apply the changes */
	mailValues = g_strsplit(deliver_to,",",1);
	
	addForw.mod_op = LDAP_MOD_ADD;
	addForw.mod_type = _ldap_cfg.field_fwdtarget;
	addForw.mod_values = mailValues;
	
	modify[0] = &addForw;
	modify[1] = NULL;
			
	TRACE(TRACE_DEBUG, "creating additional forward [%s] -> [%s]", alias, deliver_to);
	
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, modify);
	
	g_strfreev(mailValues);
	ldap_memfree(_ldap_dn);
	
	if (_ldap_err) {
		TRACE(TRACE_ERROR, "update failed: %s", ldap_err2string(_ldap_err));
		return -1;
	}
	
	return 0;
}	

static int forward_delete(const char *alias, const char *deliver_to)
{
	char **mailValues = NULL;
	LDAPMod *modify[2], delForw;
	GString *t=g_string_new("");

	g_string_printf(t,"%s=%s,%s", _ldap_cfg.cn_string, alias, _ldap_cfg.base_dn);
	_ldap_dn=g_strdup(t->str);
	g_string_free(t,TRUE);

	/* construct and apply the changes */
	mailValues = g_strsplit(deliver_to,",",1);
	
	delForw.mod_op = LDAP_MOD_DELETE;
	delForw.mod_type = _ldap_cfg.field_fwdtarget;
	delForw.mod_values = mailValues;
	
	modify[0] = &delForw;
	modify[1] = NULL;
			
	TRACE(TRACE_DEBUG, "delete additional forward [%s] -> [%s]", alias, deliver_to);
	_ldap_err = ldap_modify_s(_ldap_conn, _ldap_dn, modify);
	
	g_strfreev(mailValues);
	
	if (_ldap_err) {
		TRACE(TRACE_DEBUG, "delete additional forward failed, removing dn [%s]", _ldap_dn);
		_ldap_err = ldap_delete_s(_ldap_conn, _ldap_dn);
		if (_ldap_err)
			TRACE(TRACE_ERROR, "deletion failed [%s]", ldap_err2string(_ldap_err));
	}
	
	ldap_memfree(_ldap_dn);
	return 0;

}
int auth_addalias_ext(const char *alias, const char *deliver_to, u64_t clientid UNUSED)
{
	switch(forward_exists(alias,deliver_to)) {
		case -1:
			return forward_create(alias,deliver_to);
		case 1:
			return forward_add(alias,deliver_to);
	}
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

	if (! (userid = auth_get_userid(user_idnr)))
		return -1;
	
	/* check the alias against the known aliases for this user */
	aliases = auth_get_user_aliases(user_idnr);
	aliases = g_list_first(aliases);
	while (aliases) {
		if (strcmp(alias,(char *)aliases->data)==0)
			break;
		
		if (! g_list_next(aliases))
			break;
		aliases = g_list_next(aliases);
		
	}
	if (!aliases) {
		TRACE(TRACE_DEBUG,"alias [%s] for user [%s] not found", alias, userid);
				
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
		TRACE(TRACE_ERROR, "update failed: %s", ldap_err2string(_ldap_err));
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
	
	int check;

	check = forward_exists(alias,deliver_to);
	if (check)
		return 0;
	
	return forward_delete(alias,deliver_to);
}

gboolean auth_requires_shadow_user(void)
{
	return TRUE;
}

