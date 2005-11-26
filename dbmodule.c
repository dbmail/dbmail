
/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libdbmysql or libdbpgsql and
 * populate the global 'db' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#include <gmodule.h>

#include "config.h"
#include "dbmailtypes.h"
#include "debug.h"
#include "db.h"

db_func_t *db;

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

	if (!g_module_supported())
		return 1;

	db = (db_func_t *)dm_malloc(sizeof(db_func_t));
	if (!db) {
		trace(TRACE_FATAL, "db_init: cannot allocate memory");
		return -3;
	}
	memset(db, 0, sizeof(db_func_t));

	if (strcasecmp(_db_params.driver, "PGSQL") == 0)
		lib = "dbpgsql";
	else if (strcasecmp(_db_params.driver, "POSTGRESQL") == 0)
		lib = "dbpgsql";
	else if (strcasecmp(_db_params.driver, "MYSQL") == 0)
		lib = "dbmysql";
	else if (strcasecmp(_db_params.driver, "SQLITE") == 0)
		lib = "dbsqlite";
	else
		trace(TRACE_FATAL, "db_init: unsupported driver: %s,"
				" please choose from MySQL, PGSQL, SQLite",
				_db_params.driver);

	lib = g_module_build_path("pgsql/.libs", lib);
	module = g_module_open(lib, G_MODULE_BIND_LAZY);
	if (!module) {
		trace(TRACE_FATAL, "db_init: cannot load %s", lib);
		return -1;
	}

	if (!g_module_symbol(module, "db_connect",             &db->connect             )
	||  !g_module_symbol(module, "db_disconnect",          &db->disconnect          )
	||  !g_module_symbol(module, "db_check_connection",    &db->check_connection    )
	||  !g_module_symbol(module, "db_query",               &db->query               )
	||  !g_module_symbol(module, "db_insert_result",       &db->insert_result       )
	||  !g_module_symbol(module, "db_num_rows",            &db->num_rows            )
	||  !g_module_symbol(module, "db_num_fields",          &db->num_fields          )
	||  !g_module_symbol(module, "db_get_result",          &db->get_result          )
	||  !g_module_symbol(module, "db_free_result",         &db->free_result         )
	||  !g_module_symbol(module, "db_escape_string",       &db->escape_string       )
	||  !g_module_symbol(module, "db_escape_binary",       &db->escape_binary       )
	||  !g_module_symbol(module, "db_do_cleanup",          &db->do_cleanup          )
	||  !g_module_symbol(module, "db_get_length",          &db->get_length          )
	||  !g_module_symbol(module, "db_get_affected_rows",   &db->get_affected_rows   )
	||  !g_module_symbol(module, "db_use_msgbuf_result",   &db->use_msgbuf_result   )
	||  !g_module_symbol(module, "db_store_msgbuf_result", &db->store_msgbuf_result )
	||  !g_module_symbol(module, "db_set_result_set",      &db->set_result_set      )) {
		trace(TRACE_FATAL, "db_init: error loading %s: %s", lib, g_module_error());
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
	return db->disconnect();
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

