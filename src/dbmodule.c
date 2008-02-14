/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libdbmysql or libdbpgsql and
 * populate the global 'db' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 *
 * 
 */

#include "dbmail.h"
#define THIS_MODULE "db"

static db_func_t *db = NULL;

extern db_param_t * _db_params;

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
		TRACE(TRACE_FATAL, "loadable modules unsupported on this platform");
		return 1;
	}

	db = g_new0(db_func_t,1);

	if (strcasecmp(_db_params.driver, "PGSQL") == 0)
		driver = "pgsql";
	else if (strcasecmp(_db_params.driver, "POSTGRESQL") == 0)
		driver = "pgsql";
	else if (strcasecmp(_db_params.driver, "MYSQL") == 0)
		driver = "mysql";
	else if (strcasecmp(_db_params.driver, "SQLITE") == 0)
		driver = "sqlite";
	else if (strcasecmp(_db_params.driver, "INGRES") == 0)
		driver = "mod_ingres";

	else
		TRACE(TRACE_FATAL, "unsupported driver: %s, please choose from MySQL, PGSQL, SQLite, Ingres",
				_db_params.driver);

	field_t library_dir;
	config_get_value("library_directory", "DBMAIL", library_dir);
	if (strlen(library_dir) == 0) {
		TRACE(TRACE_DEBUG, "no value for library_directory, using default [%s]", DEFAULT_LIBRARY_DIR);
		snprintf(library_dir, sizeof(field_t), "%s", DEFAULT_LIBRARY_DIR);
	} else {
		TRACE(TRACE_DEBUG, "library_directory is [%s]", library_dir);
	}

	/* Try local build area, then dbmail lib paths, then system lib path. */
	int i;
	char *lib_path[] = { library_dir, NULL };

	/* Note that the limit here *includes* the NULL. This is intentional,
	 * to allow g_module_build_path to try the current working directory. */
	for (i = 0; i < 2; i++) {
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
		TRACE(TRACE_FATAL, "could not load db module - turn up debug level for details");
		return -1;
	}

	if (!g_module_symbol(module, "db_connect",             (gpointer)&db->connect             )
	||  !g_module_symbol(module, "db_disconnect",          (gpointer)&db->disconnect          )
	||  !g_module_symbol(module, "db_check_connection",    (gpointer)&db->check_connection    )
	||  !g_module_symbol(module, "db_query",               (gpointer)&db->query               )
	||  !g_module_symbol(module, "db_insert_result",       (gpointer)&db->insert_result       ) // deprecated: mysql-only
#if 0
	||  !g_module_symbol(module, "db_sequence_nextval",    (gpointer)&db->sequence_nextval    )
	||  !g_module_symbol(module, "db_sequence_currval",    (gpointer)&db->sequence_currval    )
#endif
	||  !g_module_symbol(module, "db_num_rows",            (gpointer)&db->num_rows            )
	||  !g_module_symbol(module, "db_num_fields",          (gpointer)&db->num_fields          )
	||  !g_module_symbol(module, "db_get_result",          (gpointer)&db->get_result          )
	||  !g_module_symbol(module, "db_free_result",         (gpointer)&db->free_result         )
	||  !g_module_symbol(module, "db_escape_string",       (gpointer)&db->escape_string       )
	||  !g_module_symbol(module, "db_escape_binary",       (gpointer)&db->escape_binary       )
	||  !g_module_symbol(module, "db_savepoint",           (gpointer)&db->savepoint           )
	||  !g_module_symbol(module, "db_savepoint_rollback",  (gpointer)&db->savepoint_rollback  )
	||  !g_module_symbol(module, "db_do_cleanup",          (gpointer)&db->do_cleanup          )
	||  !g_module_symbol(module, "db_get_length",          (gpointer)&db->get_length          )
	||  !g_module_symbol(module, "db_get_affected_rows",   (gpointer)&db->get_affected_rows   )
	||  !g_module_symbol(module, "db_get_sql",             (gpointer)&db->get_sql             )
	||  !g_module_symbol(module, "db_set_result_set",      (gpointer)&db->set_result_set      )) {
		TRACE(TRACE_FATAL, "cannot find function %s", g_module_error());
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
{
	/* Timing information stuffed in here so as
	 * to be common across all databases. */
	int result;
	time_t before, after;

	before = time(NULL);
	
	result = db->query(the_query);

	after = time(NULL);

	if (before == (time_t)-1 || after == (time_t)-1) {
		/* Can't log because time(2) failed. */
	} else {
		/* This is signed on the chance that ntpd ran during the query
		 * so it might look like it went back in time. */
		int elapsed = (int)((time_t) (after - before));
		TRACE(TRACE_DEBUG, "last query took [%d] seconds", elapsed);
		if (elapsed > (int)_db_params.query_time_info)
			TRACE(TRACE_INFO, "slow query [%s] took [%d] seconds", the_query, elapsed);
		if (elapsed > (int)_db_params.query_time_message)
			TRACE(TRACE_MESSAGE, "slow query [%s] took [%d] seconds", the_query, elapsed);
		if (elapsed > (int)_db_params.query_time_warning)
			TRACE(TRACE_WARNING, "slow query [%s] took [%d] seconds", the_query, elapsed);
	}

	return result;
}
u64_t db_insert_result(const char *sequence_identifier)
	{ return db->insert_result(sequence_identifier); }
#if 0
u64_t db_sequence_nextval(const char *sequence_identifier)
	{ return db->sequence_nextval(sequence_identifier); }
u64_t db_sequence_currval(const char *sequence_identifier)
	{ return db->sequence_currval(sequence_identifier); }
#endif

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
char * db_escape_binary(const char *from, unsigned long length)
	{ return db->escape_binary(from, length); }
int db_savepoint(const char *id)
	{ return db->savepoint(id); }
int db_savepoint_rollback(const char *id)
	{ return db->savepoint_rollback(id); }

int db_do_cleanup(const char **tables, int num_tables)
	{ return db->do_cleanup(tables, num_tables); }
u64_t db_get_length(unsigned row, unsigned field)
	{ return db->get_length(row, field); }
u64_t db_get_affected_rows(void)
	{ return db->get_affected_rows(); }
void db_set_result_set(void *res)
	{ db->set_result_set(res); }
const char * db_get_sql(sql_fragment_t frag)
	{ return db->get_sql(frag); }

