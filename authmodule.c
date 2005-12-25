
/* Dynamic loading of the authentication backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libauthsql or libauthldap and
 * populate the global 'auth' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#include <gmodule.h>

#include "config.h"
#include "dbmailtypes.h"
#include "debug.h"
#include "auth.h"
#include "authmodule.h"

auth_func_t *auth;

extern db_param_t _db_params;

/* Returns:
 *  1 on modules unsupported
 *  0 on success
 * -1 on failure to load module
 * -2 on missing symbols
 * -3 on memory error
 */
int auth_load_driver(void)
{
	GModule *module;
	char *lib = NULL;
	char *driver = NULL;

	if (!g_module_supported()) {
		trace(TRACE_FATAL, "sort_init: loadable modules unsupported on this platform");
		return 1;
	}

	auth = (auth_func_t *)dm_malloc(sizeof(auth_func_t));
	if (!auth) {
		trace(TRACE_FATAL, "auth_init: cannot allocate memory");
		return -3;
	}
	memset(auth, 0, sizeof(auth_func_t));

	if (strcasecmp(_db_params.authdriver, "SQL") == 0)
		driver = "authsql";
	else if (strcasecmp(_db_params.authdriver, "LDAP") == 0)
		driver = "authldap";
	else
		trace(TRACE_FATAL, "auth_init: unsupported driver: %s,"
				" please choose from SQL or LDAP",
				_db_params.authdriver);

	/* Try local build area, then dbmail lib paths, then system lib path. */
	int i;
	char *lib_path[] = {
		"modules/.libs",
		"/usr/lib/dbmail",
		"/usr/local/lib/dbmail",
		NULL };
	for (i = 0; i < 4; i++) {
		lib = g_module_build_path(lib_path[i], driver);
		module = g_module_open(lib, 0); // non-lazy bind.
		if (module)
			break;
		printf( "not found in %s\n", lib_path[i] );
	}

	/* If the list is exhausted without opening a module, we'll catch it. */
	if (!module) {
		trace(TRACE_FATAL, "auth_init: cannot load %s: %s", lib, g_module_error());
		return -1;
	}

	if (!g_module_symbol(module, "auth_connect",                &auth->connect                )
	||  !g_module_symbol(module, "auth_disconnect",             &auth->disconnect             )
	||  !g_module_symbol(module, "auth_user_exists",            &auth->user_exists            )
	||  !g_module_symbol(module, "auth_get_userid",             &auth->get_userid             )
	||  !g_module_symbol(module, "auth_get_known_users",        &auth->get_known_users        )
	||  !g_module_symbol(module, "auth_getclientid",            &auth->getclientid            )
	||  !g_module_symbol(module, "auth_getmaxmailsize",         &auth->getmaxmailsize         )
	||  !g_module_symbol(module, "auth_getencryption",          &auth->getencryption          )
	||  !g_module_symbol(module, "auth_check_user_ext",         &auth->check_user_ext         )
	||  !g_module_symbol(module, "auth_adduser",                &auth->adduser                )
	||  !g_module_symbol(module, "auth_delete_user",            &auth->delete_user            )
	||  !g_module_symbol(module, "auth_change_username",        &auth->change_username        )
	||  !g_module_symbol(module, "auth_change_password",        &auth->change_password        )
	||  !g_module_symbol(module, "auth_change_clientid",        &auth->change_clientid        )
	||  !g_module_symbol(module, "auth_change_mailboxsize",     &auth->change_mailboxsize     )
	||  !g_module_symbol(module, "auth_validate",               &auth->validate               )
	||  !g_module_symbol(module, "auth_md5_validate",           &auth->md5_validate           )
	||  !g_module_symbol(module, "auth_get_users_from_clientid",&auth->get_users_from_clientid)
	||  !g_module_symbol(module, "auth_get_deliver_from_alias", &auth->get_deliver_from_alias )
	||  !g_module_symbol(module, "auth_get_user_aliases",       &auth->get_user_aliases       )
	||  !g_module_symbol(module, "auth_addalias",               &auth->addalias               )
	||  !g_module_symbol(module, "auth_addalias_ext",           &auth->addalias_ext           )
	||  !g_module_symbol(module, "auth_removealias",            &auth->removealias            )
	||  !g_module_symbol(module, "auth_removealias_ext",        &auth->removealias_ext        )
	||  !g_module_symbol(module, "auth_requires_shadow_user",   &auth->requires_shadow_user   )) {
		trace(TRACE_FATAL, "auth_init: cannot find function: %s: %s", lib, g_module_error());
		return -2;
	}

	return 0;
}

/* This is the first auth_* call anybody should make. */
int auth_connect(void)
{
	auth_load_driver();
	return auth->connect();
}

/* But sometimes this gets called after help text or an
 * error but without a matching auth_connect before it. */
int auth_disconnect(void)
{
	if (!auth) return 0;
	return auth->disconnect();
}

int auth_user_exists(const char *username, u64_t * user_idnr)
	{ return auth->user_exists(username, user_idnr); }
char *auth_get_userid(u64_t user_idnr)
	{ return auth->get_userid(user_idnr); }
GList * auth_get_known_users(void)
	{ return auth->get_known_users(); }
int auth_getclientid(u64_t user_idnr, u64_t * client_idnr)
	{ return auth->getclientid(user_idnr, client_idnr); }
int auth_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size)
	{ return auth->getmaxmailsize(user_idnr, maxmail_size); }
char *auth_getencryption(u64_t user_idnr)
	{ return auth->getencryption(user_idnr); }
int auth_check_user_ext(const char *username, struct dm_list *userids,
		struct dm_list *fwds, int checks)
	{ return auth->check_user_ext(username, userids, fwds, checks); }
int auth_adduser(const char *username, const char *password, const char *enctype,
		u64_t clientid, u64_t maxmail, u64_t * user_idnr)
	{ return auth->adduser(username, password, enctype,
			clientid, maxmail, user_idnr); }
int auth_delete_user(const char *username)
	{ return auth->delete_user(username); }
int auth_change_username(u64_t user_idnr, const char *new_name)
	{ return auth->change_username(user_idnr, new_name); }
int auth_change_password(u64_t user_idnr,
		const char *new_pass, const char *enctype)
	{ return auth->change_password(user_idnr, new_pass, enctype); }
int auth_change_clientid(u64_t user_idnr, u64_t new_cid)
	{ return auth->change_clientid(user_idnr, new_cid); }
int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
	{ return auth->change_mailboxsize(user_idnr, new_size); }
int auth_validate(clientinfo_t *ci, char *username, char *password, u64_t * user_idnr)
	{ return auth->validate(ci, username, password, user_idnr); }
u64_t auth_md5_validate(clientinfo_t *ci, char *username,
		unsigned char *md5_apop_he, char *apop_stamp)
	{ return auth->md5_validate(ci, username,
			md5_apop_he, apop_stamp); }
int auth_get_users_from_clientid(u64_t client_id, 
		u64_t ** user_ids, unsigned *num_users)
	{ return auth->get_users_from_clientid(client_id,
			user_ids, num_users); }
char *auth_get_deliver_from_alias(const char *alias)
	{ return auth->get_deliver_from_alias(alias); }
GList * auth_get_user_aliases(u64_t user_idnr)
	{ return auth->get_user_aliases(user_idnr); }
int auth_addalias(u64_t user_idnr, const char *alias, u64_t clientid)
	{ return auth->addalias(user_idnr, alias, clientid); }
int auth_addalias_ext(const char *alias, const char *deliver_to,
		u64_t clientid)
	{ return auth->addalias_ext(alias, deliver_to, clientid); }
int auth_removealias(u64_t user_idnr, const char *alias)
	{ return auth->removealias(user_idnr, alias); }
int auth_removealias_ext(const char *alias, const char *deliver_to)
	{ return auth->removealias_ext(alias, deliver_to); }
gboolean auth_requires_shadow_user(void)
	{ return auth->requires_shadow_user(); }

