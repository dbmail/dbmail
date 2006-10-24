
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
				__FILE__, __func__);
		return 1;
	}

	sort = g_new0(sort_func_t,1);
	if (!sort) {
		trace(TRACE_FATAL, "%s,%s: cannot allocate memory",
				__FILE__, __func__);
		return -3;
	}

	/* The only supported driver is Sieve. */
	driver = "sort_sieve";

	/* Try local build area, then dbmail lib paths, then system lib path. */
	int i;
	char *lib_path[] = {
		"modules/.libs",
		PREFIX "/lib/dbmail",
		NULL };
	for (i = 0; i < 4; i++) {
		lib = g_module_build_path(lib_path[i], driver);
		module = g_module_open(lib, 0); // non-lazy bind.
		if (module)
			break;
	}

	/* If the list is exhausted without opening a module, we'll catch it,
	 * but we don't bomb out as we do for db and auth; just deliver normally. */
	if (!module) {
		trace(TRACE_ERROR, "%s,%s: cannot load %s: %s", 
				__FILE__, __func__, 
				lib, g_module_error());
		return -1;
	}

	if (!g_module_symbol(module, "sort_process",                (gpointer)&sort->process                )
	||  !g_module_symbol(module, "sort_validate",               (gpointer)&sort->validate               )
	||  !g_module_symbol(module, "sort_free_result",            (gpointer)&sort->free_result            )
	||  !g_module_symbol(module, "sort_listextensions",         (gpointer)&sort->listextensions         )
	||  !g_module_symbol(module, "sort_get_cancelkeep",         (gpointer)&sort->get_cancelkeep         )
	||  !g_module_symbol(module, "sort_get_reject",             (gpointer)&sort->get_reject             )
	||  !g_module_symbol(module, "sort_get_errormsg",           (gpointer)&sort->get_errormsg           )
	||  !g_module_symbol(module, "sort_get_error",              (gpointer)&sort->get_error              )
	||  !g_module_symbol(module, "sort_get_mailbox",            (gpointer)&sort->get_mailbox            )) {
		trace(TRACE_ERROR, "%s,%s: cannot find function: %s: Did you enable SIEVE"
				" sorting in the DELIVERY section of dbmail.conf but forget"
				" to build the Sieve loadable module?", 
				__FILE__, __func__, g_module_error());
		return -2;
	}

	return 0;
}

sort_result_t *sort_process(u64_t user_idnr, struct DbmailMessage *message)
{
	if (!sort)
		sort_load_driver();
	if (!sort->process) {
		trace(TRACE_ERROR, "%s, %s: Error loading sort driver",
				__FILE__, __func__);
		return NULL;
	}
	return sort->process(user_idnr, message);
}

sort_result_t *sort_validate(u64_t user_idnr, char *scriptname)
{
	if (!sort)
		sort_load_driver();
	if (!sort->validate) {
		trace(TRACE_ERROR, "%s, %s: Error loading sort driver",
				__FILE__, __func__);
		return NULL;
	}
	return sort->validate(user_idnr, scriptname);
}

const char *sort_listextensions(void)
{
	if (!sort)
		sort_load_driver();
	if (!sort->listextensions) {
		trace(TRACE_ERROR, "%s, %s: Error loading sort driver",
				__FILE__, __func__);
		return NULL;
	}
	return sort->listextensions();
}

void sort_free_result(sort_result_t *result)
{
	if (!sort->free_result)
		return;
	return sort->free_result(result);
}

int sort_get_cancelkeep(sort_result_t *result)
{
	if (!sort->get_cancelkeep)
		return 0;
	return sort->get_cancelkeep(result);
}

int sort_get_reject(sort_result_t *result)
{
	if (!sort->get_reject)
		return 0;
	return sort->get_reject(result);
}

const char * sort_get_mailbox(sort_result_t *result)
{
	if (!sort->get_mailbox)
		return "";
	return sort->get_mailbox(result);
}

const char * sort_get_errormsg(sort_result_t *result)
{
	if (!sort->get_errormsg)
		return "";
	return sort->get_errormsg(result);
}

int sort_get_error(sort_result_t *result)
{
	if (!sort->get_error)
		return 0;
	return sort->get_error(result);
}

