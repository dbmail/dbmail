/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libdbmysql or libdbpgsql and
 * populate the global 'db' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 *
 * $Id: $
 */

#include <gmodule.h>

#include "config.h"
#include "debug.h"
#include "db.h"
#include "dbmodule.h"

db_func_t *db = NULL;

extern db_param_t _db_params;

/* Returns:
 *  1 on modules unsupported
 *  0 on success
 * -1 on failure to load module
 * -2 on missing symbols
 * -3 on memory error
 */
int db_load_driver(void)
{
	GModule *module;
	char *lib = NULL;
	char *driver = NULL;

	if (!g_module_supported()) {
		trace(TRACE_FATAL, "%s,%s: loadable modules unsupported on this platform",
				__FILE__, __func__);
		return 1;
	}

	db = g_new0(db_func_t,1);
	if (!db) {
		trace(TRACE_FATAL, "%s,%s: cannot allocate memory",
				__FILE__, __func__);
		return -3;
	}

	if (strcasecmp(_db_params.driver, "PGSQL") == 0)
		driver = "pgsql";
	else if (strcasecmp(_db_params.driver, "POSTGRESQL") == 0)
		driver = "pgsql";
	else if (strcasecmp(_db_params.driver, "MYSQL") == 0)
		driver = "mysql";
	else if (strcasecmp(_db_params.driver, "SQLITE") == 0)
		driver = "sqlite";
	else
		trace(TRACE_FATAL, "%s,%s: unsupported driver: %s,"
				" please choose from MySQL, PGSQL, SQLite",
				__FILE__, __func__,
				_db_params.driver);

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

	if (!g_module_symbol(module, "db_connect",             (gpointer)&db->connect             )
	||  !g_module_symbol(module, "db_disconnect",          (gpointer)&db->disconnect          )
	||  !g_module_symbol(module, "db_check_connection",    (gpointer)&db->check_connection    )
	||  !g_module_symbol(module, "db_query",               (gpointer)&db->query               )
	||  !g_module_symbol(module, "db_insert_result",       (gpointer)&db->insert_result       )
	||  !g_module_symbol(module, "db_num_rows",            (gpointer)&db->num_rows            )
	||  !g_module_symbol(module, "db_num_fields",          (gpointer)&db->num_fields          )
	||  !g_module_symbol(module, "db_get_result",          (gpointer)&db->get_result          )
	||  !g_module_symbol(module, "db_free_result",         (gpointer)&db->free_result         )
	||  !g_module_symbol(module, "db_escape_string",       (gpointer)&db->escape_string       )
	||  !g_module_symbol(module, "db_escape_binary",       (gpointer)&db->escape_binary       )
	||  !g_module_symbol(module, "db_do_cleanup",          (gpointer)&db->do_cleanup          )
	||  !g_module_symbol(module, "db_get_length",          (gpointer)&db->get_length          )
	||  !g_module_symbol(module, "db_get_affected_rows",   (gpointer)&db->get_affected_rows   )
	||  !g_module_symbol(module, "db_use_msgbuf_result",   (gpointer)&db->use_msgbuf_result   )
	||  !g_module_symbol(module, "db_store_msgbuf_result", (gpointer)&db->store_msgbuf_result )
	||  !g_module_symbol(module, "db_get_sql",             (gpointer)&db->get_sql      	)
	||  !g_module_symbol(module, "db_set_result_set",      (gpointer)&db->set_result_set      )) {
		trace(TRACE_FATAL, "%s,%s: cannot find function: %s: %s", 
				__FILE__, __func__, lib, g_module_error());
		return -2;
	}

	return 0;
}

/* This is the first db_* call anybody should make. */
int db_connect(void)
{
	db_load_driver();
	return db->connect();
}

/* But sometimes this gets called after help text or an
 * error but without a matching db_connect before it. */
int db_disconnect(void)
{
	if (!db) return 0;
	db->disconnect();
	g_free(db);
	return 0;
}

int db_check_connection(void)
	{ return db->check_connection(); }
int db_query(const char *the_query)
	{ return db->query(the_query); }
u64_t db_insert_result(const char *sequence_identifier)
	{ return db->insert_result(sequence_identifier); }
unsigned db_num_rows(void)
	{ return db->num_rows(); }
unsigned db_num_fields(void)
	{ return db->num_fields(); }
const char * db_get_result(unsigned row, unsigned field)
	{ return db->get_result(row, field); }
void db_free_result(void)
	{ return db->free_result(); }
unsigned long db_escape_string(char *to,
		const char *from, unsigned long length)
	{ return db->escape_string(to, from, length); }
unsigned long db_escape_binary(char *to,
		const char *from, unsigned long length)
	{ return db->escape_binary(to, from, length); }
int db_do_cleanup(const char **tables, int num_tables)
	{ return db->do_cleanup(tables, num_tables); }
u64_t db_get_length(unsigned row, unsigned field)
	{ return db->get_length(row, field); }
u64_t db_get_affected_rows(void)
	{ return db->get_affected_rows(); }
void db_use_msgbuf_result(void)
	{ db->use_msgbuf_result(); }
void db_store_msgbuf_result(void)
	{ db->store_msgbuf_result(); }
void db_set_result_set(void *res)
	{ db->set_result_set(res); }
const char * db_get_sql(sql_fragment_t frag)
	{ return db->get_sql(frag); }

