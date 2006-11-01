/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

 Modified 2004 by Mikhail Ramendik mr@ramendik.ru (MR)

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 *
 * mysql driver file.
 * functions for connecting and talking to the Mysql database
 */

#include "dbmail.h"
#include "mysql.h"

#define THIS_MODULE "sql"

#define DB_MYSQL_STANDARD_PORT 3306

const char * db_get_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "DATE_FORMAT(%s, '%%Y-%%m-%%d %%T')";
		break;
		case SQL_TO_DATE:
			return "'%s'";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "CURRENT_TIMESTAMP";
		break;
		case SQL_REPLYCACHE_EXPIRE:
			return "NOW() - INTERVAL %d DAY";
		break;
		case SQL_BINARY:
			return "BINARY";
		break;
		case SQL_REGEXP:
			return "REGEXP";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE BINARY";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_ENCODE_ESCAPE:
			return "%s";
		break;
	}
	return NULL;
}

static MYSQL conn; /**< MySQL database connection */
static MYSQL_RES *res = NULL; /**< MySQL result set */
static MYSQL_ROW last_row; /**< MySQL result row */

/* database parameters */
db_param_t _db_params;

/* static functions, only used locally */

/* Check that the database and connection collations match.
 * I don't think we need to worry about the server collation. */
static int db_mysql_check_collations(void)
{
	char the_query[DEF_QUERYSIZE];
	char *collation[3][2];
	int collations_match = 0;
	int i, j;

	if (strlen(_db_params.encoding) > 0) {
		snprintf(the_query, DEF_QUERYSIZE, "SET NAMES %s", _db_params.encoding);
		if (db_query(the_query) == DM_EQUERY) {
			TRACE(TRACE_ERROR, "error setting collation");
			return DM_EQUERY;
		}
		db_free_result();
	}

	snprintf(the_query, DEF_QUERYSIZE, "SHOW VARIABLES LIKE 'collation_%%'");
	if (db_query(the_query) == DM_EQUERY) {
		TRACE(TRACE_ERROR, "error getting collation variables for database");
		return DM_EQUERY;
	}

	for (i = 0; i < 3; i++)
	for (j = 0; j < 2; j++)
		collation[i][j] = strdup(db_get_result(i, j));

	/* Sane indentation goes insane here. */
	for (i = 0; i < 3; i++)
	if (strcmp(collation[i][0], "collation_database") == 0) {
		for (j = 0; j < 3; j++)
		if (strcmp(collation[j][0], "collation_connection") == 0) {
			TRACE(TRACE_DEBUG, "does [%s:%s] match [%s:%s]?",
				collation[i][0], collation[i][1],
				collation[j][0], collation[j][1]);
			if (strcmp(collation[i][1], collation[j][1]) == 0) {
				collations_match = 1;
				break;
			}
		}
		if (collations_match)
			break;
	}

	db_free_result();
	for (i = 0; i < 3; i++)
	for (j = 0; j < 2; j++)
		free(collation[i][j]);

	if (!collations_match) {
		TRACE(TRACE_ERROR, "collation mismatch, your MySQL configuration specifies a"
		      " different charset than the data currently in your DBMail database.");
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}


int db_connect()
{
	char *sock = NULL;
	/* connect */
	mysql_init(&conn);

	/* auto re-connect */
	conn.reconnect = 1;

	/* use the standard MySQL port by default */
	if (_db_params.port == 0)
		_db_params.port = DB_MYSQL_STANDARD_PORT;
	/* issue an error if a connection to a MySQL instance 
	 * on the localhost is requested, but no sqlsocket is given */
	if (strncmp(_db_params.host, "localhost", FIELDSIZE) == 0 ||
	    _db_params.host == NULL) {
		if (strlen(_db_params.sock) == 0) {
			TRACE(TRACE_WARNING, "MySQL host is set to "
			      "localhost, but no mysql_socket has been set. "
			      "Use sqlsocket=... in dbmail.conf. Connecting "
			      "will be attempted using the default socket.");
			sock = NULL;
		} else
			sock = _db_params.sock;
	}


	if (mysql_real_connect(&conn, _db_params.host, _db_params.user,
			       _db_params.pass, _db_params.db,
			       _db_params.port, sock, 0) == NULL) {
		TRACE(TRACE_ERROR, "mysql_real_connect failed: %s", mysql_error(&conn));
		return DM_EQUERY;
	}

	if (db_mysql_check_collations() == DM_EQUERY)
		return DM_EQUERY;

	return DM_SUCCESS;
}

unsigned db_num_rows()
{
	/* mysql_store_result can return NULL. If this is
	 * true, res will be zero, and naturally num_rows
	 * should return DM_SUCCESS */
	if (!res)
		return DM_SUCCESS;

	return mysql_num_rows(res);
}

unsigned db_num_fields()
{
	if (!res)
		return DM_SUCCESS;

	return mysql_num_fields(res);
}

void db_free_result()
{
	if (res)
		mysql_free_result(res);
	
	res = NULL;
}


const char *db_get_result(unsigned row, unsigned field)
{
	char *result;

	if (!res) {
		TRACE(TRACE_WARNING, "result set is null\n");
		return NULL;
	}

	if ((row > db_num_rows()) || (field > db_num_fields())) {
		TRACE(TRACE_WARNING, "row = %u or field = %u out of range", row, field);
		return NULL;
	}
	
	mysql_data_seek(res, row);
	last_row = mysql_fetch_row(res);
	
	if (last_row == NULL) {
		TRACE(TRACE_DEBUG, "row is NULL");
		return NULL;
	}

	result = last_row[field];
	if (result == NULL)
		TRACE(TRACE_DEBUG, "result is null");
	return result;
}

int db_disconnect()
{
	if (res)
		db_free_result();
	mysql_close(&conn);
	return DM_SUCCESS;
}

int db_check_connection()
{
	if (mysql_ping(&conn)) {
		if (db_connect() < 0) {
			TRACE(TRACE_ERROR, "unable to connect to database.");
			return DM_EQUERY;
		}
	}
	return DM_SUCCESS;
}

u64_t db_insert_result(const char *sequence_identifier UNUSED)
{
	u64_t insert_result;
	insert_result = mysql_insert_id(&conn);
	return insert_result;
}

int db_query(const char *q)
{
	unsigned querysize = 0;
	assert(q);

	querysize = (unsigned) strlen(q);

	g_return_val_if_fail(querysize > 0, DM_EQUERY);

	if (db_check_connection() < 0) 
		return DM_EQUERY;
	
	TRACE(TRACE_DEBUG, "query [%s]", q);
	if (mysql_real_query(&conn, q, querysize)) {
		TRACE(TRACE_ERROR, "[%s] [%s]", mysql_error(&conn), q);
		return DM_EQUERY;
	}

	if(res != NULL) 
		db_free_result();
	
	res = mysql_store_result(&conn);

	return DM_SUCCESS;
}

unsigned long db_escape_string(char *to, const char *from, unsigned long length)
{
	return mysql_real_escape_string(&conn, to, from, length);
}

unsigned long db_escape_binary(char *to, const char *from, unsigned long length)
{
	return db_escape_string(to,from,length);
}

int db_do_cleanup(const char **tables, int num)
{
	char the_query[DEF_QUERYSIZE];
	int i;
	int result = 0;

	for (i = 0; i < num; i++) {
		snprintf(the_query, DEF_QUERYSIZE, "OPTIMIZE TABLE %s%s",
			 _db_params.pfx,tables[i]);

		if (db_query(the_query) == DM_EQUERY) {
			TRACE(TRACE_ERROR, "error optimizing table [%s%s]",
			      _db_params.pfx,tables[i]);
			result = DM_EQUERY;
		}
		db_free_result();
	}

	return result;
}

u64_t db_get_length(unsigned row, unsigned field)
{
	if (!res) {
		TRACE(TRACE_WARNING, "result set is null");
		return DM_EQUERY;
	}


	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		TRACE(TRACE_ERROR, "row = %u, field = %u, bigger than size of result set",
		      row, field);
		return DM_EQUERY;
	}
	
	mysql_data_seek(res, row);
	last_row = mysql_fetch_row(res);
	if (last_row == NULL) {
		TRACE(TRACE_ERROR, "last_row = NULL");
		return (u64_t) 0;
	}
	return (u64_t) mysql_fetch_lengths(res)[field];
}

u64_t db_get_affected_rows()
{
	return (u64_t) mysql_affected_rows(&conn);
}

void *db_get_result_set()
{
	return (void *) res;
}

void db_set_result_set(void *the_result_set)
{
	res = (MYSQL_RES *) the_result_set;
}
