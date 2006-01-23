
/* Dynamic loading of the sort backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libsortsieve and
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
		driver = "sort_sieve";
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
	}

	/* If the list is exhausted without opening a module, we'll catch it. */
	if (!module) {
		trace(TRACE_FATAL, "db_init: cannot load %s: %s", lib, g_module_error());
		return -1;
	}

	if (!g_module_symbol(module, "sort_connect",                (gpointer)&sort->connect                )
	||  !g_module_symbol(module, "sort_disconnect",             (gpointer)&sort->disconnect             )
	||  !g_module_symbol(module, "sort_validate",               (gpointer)&sort->validate               )
	||  !g_module_symbol(module, "sort_process",                (gpointer)&sort->process                )) {
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

int sort_validate(u64_t user_idnr, char *scriptname, char **errmsg)
{
	return sort->validate(user_idnr, scriptname, errmsg);
}

int sort_process(u64_t user_idnr, struct DbmailMessage *message,
		const char *fromaddr)
{
	return sort->process(user_idnr, message, fromaddr);
}

