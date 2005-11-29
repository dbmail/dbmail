
/* Dynamic loading of the sortentication backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libsortsql or libsortldap and
 * populate the global 'sort' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#include <gmodule.h>

#include "config.h"
#include "dbmailtypes.h"
#include "debug.h"
#include "sort.h"
#include "sortmodule.h"

sort_func_t *sort;

extern db_param_t _db_params;

/* Returns:
 *  1 on modules unsupported
 *  0 on success
 * -1 on failure to load module
 * -2 on missing symbols
 * -3 on memory error
 */
int sort_load_driver(void)
{
	GModule *module;
	char *lib = NULL;
	char *driver = NULL;

	if (!g_module_supported()) {
		trace(TRACE_FATAL, "sort_init: loadable modules unsupported on this platform");
		return 1;
	}

	sort = (sort_func_t *)dm_malloc(sizeof(sort_func_t));
	if (!sort) {
		trace(TRACE_FATAL, "sort_init: cannot allocate memory");
		return -3;
	}
	memset(sort, 0, sizeof(sort_func_t));

	if (strcasecmp(_db_params.sortdriver, "SIEVE") == 0)
		driver = "sortsieve";
	else if (strcasecmp(_db_params.sortdriver, "LDAP") == 0)
		driver = "";
	else
		trace(TRACE_FATAL, "sort_init: unsupported driver: %s,"
				" please choose SIEVE or none at all",
				_db_params.sortdriver);

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
		trace(TRACE_FATAL, "db_init: cannot load %s: %s", lib, g_module_error());
		return -1;
	}

	if (!g_module_symbol(module, "sort_connect",                &sort->connect                )
	||  !g_module_symbol(module, "sort_disconnect",             &sort->disconnect             )
	||  !g_module_symbol(module, "sort_user_exists",            &sort->user_exists            )
	||  !g_module_symbol(module, "sort_get_userid",             &sort->get_userid             )
	||  !g_module_symbol(module, "sort_get_known_users",        &sort->get_known_users        )
	||  !g_module_symbol(module, "sort_getclientid",            &sort->getclientid            )
	||  !g_module_symbol(module, "sort_getmaxmailsize",         &sort->getmaxmailsize         )
	||  !g_module_symbol(module, "sort_getencryption",          &sort->getencryption          )
	||  !g_module_symbol(module, "sort_check_user_ext",         &sort->check_user_ext         )
	||  !g_module_symbol(module, "sort_adduser",                &sort->adduser                )
	||  !g_module_symbol(module, "sort_delete_user",            &sort->delete_user            )
	||  !g_module_symbol(module, "sort_change_username",        &sort->change_username        )
	||  !g_module_symbol(module, "sort_change_password",        &sort->change_password        )
	||  !g_module_symbol(module, "sort_change_clientid",        &sort->change_clientid        )
	||  !g_module_symbol(module, "sort_change_mailboxsize",     &sort->change_mailboxsize     )
	||  !g_module_symbol(module, "sort_validate",               &sort->validate               )
	||  !g_module_symbol(module, "sort_md5_validate",           &sort->md5_validate           )
	||  !g_module_symbol(module, "sort_get_users_from_clientid",&sort->get_users_from_clientid)
	||  !g_module_symbol(module, "sort_get_deliver_from_alias", &sort->get_deliver_from_alias )
	||  !g_module_symbol(module, "sort_get_user_aliases",       &sort->get_user_aliases       )
	||  !g_module_symbol(module, "sort_addalias",               &sort->addalias               )
	||  !g_module_symbol(module, "sort_addalias_ext",           &sort->addalias_ext           )
	||  !g_module_symbol(module, "sort_removealias",            &sort->removealias            )
	||  !g_module_symbol(module, "sort_removealias_ext",        &sort->removealias_ext        )) {
		trace(TRACE_FATAL, "sort_init: cannot find function: %s: %s", lib, g_module_error());
		return -2;
	}

	return 0;
}

/* This is the first sort_* call anybody should make. */
int sort_connect(void)
{
	sort_load_driver();
	return sort->connect();
}

/* But sometimes this gets called after help text or an
 * error but without a matching sort_connect before it. */
int sort_disconnect(void)
{
	if (!sort) return 0;
	return sort->disconnect();
}

int sort_user_exists(const char *username, u64_t * user_idnr)
	{ return sort->user_exists(username, user_idnr); }
char *sort_get_userid(u64_t user_idnr)
	{ return sort->get_userid(user_idnr); }
GList * sort_get_known_users(void)
	{ return sort->get_known_users(); }
int sort_getclientid(u64_t user_idnr, u64_t * client_idnr)
	{ return sort->getclientid(user_idnr, client_idnr); }
int sort_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size)
	{ return sort->getmaxmailsize(user_idnr, maxmail_size); }
char *sort_getencryption(u64_t user_idnr)
	{ return sort->getencryption(user_idnr); }
int sort_check_user_ext(const char *username, struct dm_list *userids,
		struct dm_list *fwds, int checks)
	{ return sort->check_user_ext(username, userids, fwds, checks); }
int sort_adduser(const char *username, const char *password, const char *enctype,
		u64_t clientid, u64_t maxmail, u64_t * user_idnr)
	{ return sort->adduser(username, password, enctype,
			clientid, maxmail, user_idnr); }
int sort_delete_user(const char *username)
	{ return sort->delete_user(username); }
int sort_change_username(u64_t user_idnr, const char *new_name)
	{ return sort->change_username(user_idnr, new_name); }
int sort_change_password(u64_t user_idnr,
		const char *new_pass, const char *enctype)
	{ return sort->change_password(user_idnr, new_pass, enctype); }
int sort_change_clientid(u64_t user_idnr, u64_t new_cid)
	{ return sort->change_clientid(user_idnr, new_cid); }
int sort_change_mailboxsize(u64_t user_idnr, u64_t new_size)
	{ return sort->change_mailboxsize(user_idnr, new_size); }
int sort_validate(clientinfo_t *ci, char *username, char *password, u64_t * user_idnr)
	{ return sort->validate(ci, username, password, user_idnr); }
u64_t sort_md5_validate(clientinfo_t *ci, char *username,
		unsigned char *md5_apop_he, char *apop_stamp)
	{ return sort->md5_validate(ci, username,
			md5_apop_he, apop_stamp); }
int sort_get_users_from_clientid(u64_t client_id, 
		u64_t ** user_ids, unsigned *num_users)
	{ return sort->get_users_from_clientid(client_id,
			user_ids, num_users); }
char *sort_get_deliver_from_alias(const char *alias)
	{ return sort->get_deliver_from_alias(alias); }
GList * sort_get_user_aliases(u64_t user_idnr)
	{ return sort->get_user_aliases(user_idnr); }
int sort_addalias(u64_t user_idnr, const char *alias, u64_t clientid)
	{ return sort->addalias(user_idnr, alias, clientid); }
int sort_addalias_ext(const char *alias, const char *deliver_to,
		u64_t clientid)
	{ return sort->addalias_ext(alias, deliver_to, clientid); }
int sort_removealias(u64_t user_idnr, const char *alias)
	{ return sort->removealias(user_idnr, alias); }
int sort_removealias_ext(const char *alias, const char *deliver_to)
	{ return sort->removealias_ext(alias, deliver_to); }
