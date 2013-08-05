
/* Dynamic loading of the authentication backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libauthsql or libauthldap and
 * populate the global 'auth' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#include "dbmail.h"
#define THIS_MODULE "auth"

static auth_func_t *auth = NULL;

extern DBParam_T db_params;

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
		TRACE(TRACE_EMERG, "loadable modules unsupported on this platform");
		return 1;
	}

	auth = g_new0(auth_func_t,1);

	if (strcasecmp(db_params.authdriver, "LDAP") == 0)
		driver = "auth_ldap";
	else {
		TRACE(TRACE_DEBUG, "using default auth_sql");
		driver = "auth_sql";
	}

	Field_T library_dir;
	config_get_value("library_directory", "DBMAIL", library_dir);
	if (strlen(library_dir) == 0) {
		TRACE(TRACE_DEBUG, "no value for library_directory, using default [%s]", DEFAULT_LIBRARY_DIR);
		snprintf(library_dir, sizeof(Field_T), "%s", DEFAULT_LIBRARY_DIR);
	} else {
		TRACE(TRACE_DEBUG, "library_directory is [%s]", library_dir);
	}

	/* Try local build area, then dbmail lib paths, then system lib path. */
	int i;
	char local_path[PATH_MAX];
	memset(local_path, 0, sizeof(local_path));
	g_strlcat(local_path, DM_PWD, sizeof(local_path)-1);
	g_strlcat(local_path, "/src/modules/.libs", sizeof(local_path)-1);

	char *lib_path[] = { 
		local_path,
		library_dir, 
		NULL 
	};

	/* Note that the limit here *includes* the NULL. This is intentional,
	 * to allow g_module_build_path to try the current working directory. */
	for (i = 0; lib_path[i] != NULL; i++) {
		lib = g_module_build_path(lib_path[i], driver);
		module = g_module_open(lib, 0); // non-lazy bind.

		TRACE(TRACE_DEBUG, "looking for %s as %s", driver, lib);
		g_free(lib);

		if (!module)
			TRACE(TRACE_INFO, "cannot load %s", g_module_error());
		if (module)
			break;
	}

	/* If the list is exhausted without opening a module, we'll catch it. */
	if (!module) {
		TRACE(TRACE_EMERG, "could not load auth module - turn up debug level for details");
		return -1;
	}

	if (!g_module_symbol(module, "auth_connect",                (gpointer)&auth->connect                )
	||  !g_module_symbol(module, "auth_disconnect",             (gpointer)&auth->disconnect             )
	||  !g_module_symbol(module, "auth_user_exists",            (gpointer)&auth->user_exists            )
	||  !g_module_symbol(module, "auth_get_userid",             (gpointer)&auth->get_userid             )
	||  !g_module_symbol(module, "auth_check_userid",           (gpointer)&auth->check_userid           )
	||  !g_module_symbol(module, "auth_get_known_users",        (gpointer)&auth->get_known_users        )
	||  !g_module_symbol(module, "auth_get_known_aliases",      (gpointer)&auth->get_known_aliases      )
	||  !g_module_symbol(module, "auth_getclientid",            (gpointer)&auth->getclientid            )
	||  !g_module_symbol(module, "auth_getmaxmailsize",         (gpointer)&auth->getmaxmailsize         )
	||  !g_module_symbol(module, "auth_getencryption",          (gpointer)&auth->getencryption          )
	||  !g_module_symbol(module, "auth_check_user_ext",         (gpointer)&auth->check_user_ext         )
	||  !g_module_symbol(module, "auth_adduser",                (gpointer)&auth->adduser                )
	||  !g_module_symbol(module, "auth_delete_user",            (gpointer)&auth->delete_user            )
	||  !g_module_symbol(module, "auth_change_username",        (gpointer)&auth->change_username        )
	||  !g_module_symbol(module, "auth_change_password",        (gpointer)&auth->change_password        )
	||  !g_module_symbol(module, "auth_change_clientid",        (gpointer)&auth->change_clientid        )
	||  !g_module_symbol(module, "auth_change_mailboxsize",     (gpointer)&auth->change_mailboxsize     )
	||  !g_module_symbol(module, "auth_validate",               (gpointer)&auth->validate               )
	||  !g_module_symbol(module, "auth_md5_validate",           (gpointer)&auth->md5_validate           )
	||  !g_module_symbol(module, "auth_get_user_aliases",       (gpointer)&auth->get_user_aliases       )
	||  !g_module_symbol(module, "auth_get_aliases_ext",        (gpointer)&auth->get_aliases_ext        )
	||  !g_module_symbol(module, "auth_addalias",               (gpointer)&auth->addalias               )
	||  !g_module_symbol(module, "auth_addalias_ext",           (gpointer)&auth->addalias_ext           )
	||  !g_module_symbol(module, "auth_removealias",            (gpointer)&auth->removealias            )
	||  !g_module_symbol(module, "auth_removealias_ext",        (gpointer)&auth->removealias_ext        )
	||  !g_module_symbol(module, "auth_requires_shadow_user",   (gpointer)&auth->requires_shadow_user   )) {
		TRACE(TRACE_EMERG, "cannot find function %s", g_module_error());
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
	auth->disconnect();
	g_free(auth);
	return 0;
}

int auth_user_exists(const char *username, uint64_t * user_idnr)
	{ return auth->user_exists(username, user_idnr); }
char *auth_get_userid(uint64_t user_idnr)
	{ return auth->get_userid(user_idnr); }
int auth_check_userid(uint64_t user_idnr)
	{ return auth->check_userid(user_idnr); }
GList * auth_get_known_users(void)
	{ return auth->get_known_users(); }
GList * auth_get_known_aliases(void)
	{ return auth->get_known_aliases(); }
int auth_getclientid(uint64_t user_idnr, uint64_t * client_idnr)
	{ return auth->getclientid(user_idnr, client_idnr); }
int auth_getmaxmailsize(uint64_t user_idnr, uint64_t * maxmail_size)
	{ return auth->getmaxmailsize(user_idnr, maxmail_size); }
char *auth_getencryption(uint64_t user_idnr)
	{ return auth->getencryption(user_idnr); }
int auth_check_user_ext(const char *username, GList **userids, GList **fwds, int checks)
	{ return auth->check_user_ext(username, userids, fwds, checks); }
int auth_adduser(const char *username, const char *password, const char *enctype,
		uint64_t clientid, uint64_t maxmail, uint64_t * user_idnr)
	{ return auth->adduser(username, password, enctype,
			clientid, maxmail, user_idnr); }
int auth_delete_user(const char *username)
	{ return auth->delete_user(username); }
int auth_change_username(uint64_t user_idnr, const char *new_name)
	{ return auth->change_username(user_idnr, new_name); }
int auth_change_password(uint64_t user_idnr,
		const char *new_pass, const char *enctype)
	{ return auth->change_password(user_idnr, new_pass, enctype); }
int auth_change_clientid(uint64_t user_idnr, uint64_t new_cid)
	{ return auth->change_clientid(user_idnr, new_cid); }
int auth_change_mailboxsize(uint64_t user_idnr, uint64_t new_size)
	{ return auth->change_mailboxsize(user_idnr, new_size); }
int auth_validate(ClientBase_T *ci, const char *username, const char *password, uint64_t * user_idnr)
	{ return auth->validate(ci, username, password, user_idnr); }
uint64_t auth_md5_validate(ClientBase_T *ci, char *username,
		unsigned char *md5_apop_he, char *apop_stamp)
	{ return auth->md5_validate(ci, username,
			md5_apop_he, apop_stamp); }
GList * auth_get_user_aliases(uint64_t user_idnr)
	{ return auth->get_user_aliases(user_idnr); }
GList * auth_get_aliases_ext(const char *alias)
	{ return auth->get_aliases_ext(alias); }
int auth_addalias(uint64_t user_idnr, const char *alias, uint64_t clientid)
	{ return auth->addalias(user_idnr, alias, clientid); }
int auth_addalias_ext(const char *alias, const char *deliver_to,
		uint64_t clientid)
	{ return auth->addalias_ext(alias, deliver_to, clientid); }
int auth_removealias(uint64_t user_idnr, const char *alias)
	{ return auth->removealias(user_idnr, alias); }
int auth_removealias_ext(const char *alias, const char *deliver_to)
	{ return auth->removealias_ext(alias, deliver_to); }
gboolean auth_requires_shadow_user(void)
	{ return auth->requires_shadow_user(); }

