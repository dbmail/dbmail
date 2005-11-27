/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up libdbmysql or libdbpgsql and
 * populate the global 'db' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef DBMODULE_H
#define DBMODULE_H

/* Prototypes must match with those in db.h
 * and in the database drivers. */
typedef struct {
	int (* connect)(void);
	int (* disconnect)(void);
	int (* check_connection)(void);
	int (* query)(const char *the_query);
	u64_t (* insert_result)(const char *sequence_identifier);
	unsigned (* num_rows)(void);
	unsigned (* num_fields)(void);
	const char * (* get_result)(unsigned row, unsigned field);
	void (* free_result)(void);
	unsigned long (* escape_string)(char *to,
	                    const char *from, unsigned long length );
	unsigned long (* escape_binary)(char *to,
	       	       const char *from, unsigned long length );
	int (* do_cleanup)(const char **tables, int num_tables);
	u64_t (* get_length)(unsigned row, unsigned field);
	u64_t (* get_affected_rows)(void);
	void (* use_msgbuf_result)(void);
	void (* store_msgbuf_result)(void);
	void (* set_result_set)(void *res);
} db_func_t;

#endif
