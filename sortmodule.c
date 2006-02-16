
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

static sort_func_t *sort = NULL;

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
		trace(TRACE_FATAL, "%s,%s: loadable modules unsupported on this platform",
				__FILE__, __file__);
		return 1;
	}

	sort = g_new0(sort_func_t,1);
	if (!sort) {
		trace(TRACE_FATAL, "%s,%s: cannot allocate memory",
				__FILE__, __file__);
		return -3;
	}

	/* The only supported driver is Sieve. */
	driver = "sort_sieve";

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
		trace(TRACE_FATAL, "%s,%s: cannot load %s: %s", 
				__FILE__, __func__, 
				lib, g_module_error());
		return -1;
	}

	if (!g_module_symbol(module, "sort_process",                (gpointer)&sort->process                )
	||  !g_module_symbol(module, "sort_validate",               (gpointer)&sort->validate               )
	||  !g_module_symbol(module, "sort_free_result",            (gpointer)&sort->free_result            )
	||  !g_module_symbol(module, "sort_get_cancelkeep",         (gpointer)&sort->get_cancelkeep         )
	||  !g_module_symbol(module, "sort_get_errormsg",           (gpointer)&sort->get_errormsg           )
	||  !g_module_symbol(module, "sort_get_error",              (gpointer)&sort->get_error              )
	||  !g_module_symbol(module, "sort_get_mailbox",            (gpointer)&sort->get_mailbox            )) {
		trace(TRACE_FATAL, "%s,%s: cannot find function: %s: %s", 
				__FILE__, __func__, 
				lib, g_module_error());
		return -2;
	}

	return 0;
}

int sort_process(u64_t user_idnr, struct DbmailMessage *message)
{
	if (!sort)
		sort_load_driver();
	if (sort)
		return sort->process(user_idnr, message);
}

int sort_validate(u64_t user_idnr, char *scriptname)
{
	if (!sort)
		sort_load_driver();
	if (sort)
		return sort->validate(user_idnr, scriptname);
}

void sort_free_result(sort_result_t *result)
{
	assert(sort);
	assert(sort->free_result);
	return sort->free_result();
}

int sort_get_cancelkeep(void)
{
	assert(sort);
	assert(sort->get_cancelkeep);
	return sort->get_cancelkeep();
}

const char * sort_get_mailbox(void)
{
	assert(sort);
	assert(sort->get_mailbox);
	return sort->get_mailbox();
}

const char * sort_get_errormsg(sort_result_t *result)
{
	assert(sort);
	assert(sort->get_errormsg);
	return sort->get_errormsg();
}

int sort_get_error(sort_result_t *result)
{
	assert(sort);
	assert(sort->get_error);
	return sort->get_error();
}

