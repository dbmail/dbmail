/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "mysql.h"
#include "dbmail.h"
#include "dbmailtypes.h"

#include <string.h>

#define DB_MYSQL_STANDARD_PORT 3306

const char *TO_CHAR = "DATE_FORMAT(%s, '%%Y-%%m-%%d %%T')";
/* there is supposed to be a better way of doing this, problem is, it's
   only available from MYSQL version 4.1.1 and up, using the
   str_to_date function. For now, we'll just let MySQL deal with
   the datetime it gets. It should be able to cope with it .. */
const char *TO_DATE = "'%s'";

static MYSQL conn; /**< MySQL database connection */
static MYSQL_RES *res = NULL; /**< MySQL result set */
static MYSQL_RES *msgbuf_res = NULL; /**< MySQL result set for msgbuf */
static MYSQL_RES *stored_res = NULL; /**< MySQL result set backup */
static MYSQL_ROW last_row; /**< MySQL result row */

/** database parameters */
db_param_t _db_params;


/* static functions, only used locally */
/**
 * \brief check database connection. If it is dead, reconnect
 * \return
 *    - -1 on failure (no connection to db possible)
 *    -  0 on success
 */
static int db_check_connection(void);

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
			trace(TRACE_WARNING, "%s,%s: MySQL host is set to "
			      "localhost, but no mysql_socket has been set. "
			      "Use sqlsocket=... in dbmail.conf. Connecting "
			      "will be attempted using the default socket.",
			      __FILE__, __func__);
			sock = NULL;
		} else
			sock = _db_params.sock;
	}


	if (mysql_real_connect(&conn, _db_params.host, _db_params.user,
			       _db_params.pass, _db_params.db,
			       _db_params.port, sock, 0) == NULL) {
		trace(TRACE_ERROR, "%s,%s: mysql_real_connect failed: %s",
		      __FILE__, __func__, mysql_error(&conn));
		return -1;
	}
#ifdef mysql_errno
	if (mysql_errno(&conn)) {
		trace(TRACE_ERROR, "%s,%s: mysql_real_connect failed: %s",
		      __FILE__, __func__, mysql_error(&conn));
		return -1;
	}
#endif
	return 0;
}

unsigned db_num_rows()
{
	/* mysql_store_result can return NULL. If this is
	 * true, res will be zero, and naturally num_rows
	 * should return 0 */
	if (!res)
		return 0;

	return mysql_num_rows(res);
}

unsigned db_num_fields()
{
	if (!res)
		return 0;

	return mysql_num_fields(res);
}

void db_free_result()
{
	if (res)
		mysql_free_result(res);
	else
		trace(TRACE_WARNING, "%s,%s: Trying to free result set "
		      "that is already NULL!", __FILE__, __func__);
	res = NULL;
}


const char *db_get_result(unsigned row, unsigned field)
{
	char *result;

	if (!res) {
		trace(TRACE_WARNING, "%s,%s: result set is null\n",
		      __FILE__, __func__);
		return NULL;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		trace(TRACE_WARNING, "%s, %s: "
		      "row = %u, field = %u, bigger than size of result set",
		      __FILE__, __func__, row, field);
		return NULL;
	}
	
	mysql_data_seek(res, row);
	last_row = mysql_fetch_row(res);
	
	if (last_row == NULL) {
		trace(TRACE_WARNING, "%s,%s: row is NULL\n",
		      __FILE__, __func__);
		return NULL;
	}

	result = last_row[field];
	if (result == NULL)
		trace(TRACE_WARNING, "%s,%s: result is null\n",
		      __FILE__, __func__);
	return result;
}

int db_disconnect()
{
	if (res)
		db_free_result();
	mysql_close(&conn);
	return 0;
}

int db_check_connection()
{
	if (mysql_ping(&conn) != 0) {
		trace(TRACE_DEBUG, "%s,%s: no database connection, trying "
		      "to establish on.", __FILE__, __func__);
		if (db_connect() < 0) {
			trace(TRACE_ERROR, "%s,%s: unable to connect to "
			      "database.", __FILE__, __func__);
			return -1;
		}
	}
	return 0;
}

u64_t db_insert_result(const char *sequence_identifier UNUSED)
{
	u64_t insert_result;
	insert_result = mysql_insert_id(&conn);
	return insert_result;
}

int db_query(const char *the_query)
{
	unsigned querysize = 0;
	if (db_check_connection() < 0) {
		trace(TRACE_ERROR, "%s,%s: no database connection",
		      __FILE__, __func__);
		return -1;
	}
	if (the_query != NULL) {
		querysize = (unsigned) strlen(the_query);
		if (querysize > 0) {
			trace(TRACE_DEBUG, "%s,%s: "
			      "executing query [%s]",
			      __FILE__, __func__, the_query);
			if (mysql_real_query(&conn,
					     the_query, querysize) < 0) {
				trace(TRACE_ERROR, "%s,%s: "
				      "query [%s] failed",
				      __FILE__, __func__, the_query);
				trace(TRACE_ERROR, "%s,%s: "
				      "mysql_real_query failed: %s",
				      __FILE__, __func__,
				      mysql_error(&conn));
				return -1;
			}
		} else {
			trace(TRACE_ERROR, "%s,%s: "
			      "querysize is wrong: [%d]", __FILE__,
			      __func__, querysize);
			return -1;
		}
	} else {
		trace(TRACE_ERROR, "%s,%s: "
		      "query buffer is NULL, this is not supposed to happen\n",
		      __FILE__, __func__);
		return -1;
	}

	res = mysql_store_result(&conn);

	return 0;
}

unsigned long db_escape_string(char *to,
			       const char *from, unsigned long length)
{
	return mysql_real_escape_string(&conn, to, from, length);
}

int db_do_cleanup(const char **tables, int num)
{
	char the_query[DEF_QUERYSIZE];
	int i;
	int result = 0;

	for (i = 0; i < num; i++) {
		snprintf(the_query, DEF_QUERYSIZE, "OPTIMIZE TABLE %s",
			 tables[i]);

		if (db_query(the_query) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error optimizing table [%s]",
			      __FILE__, __func__, tables[i]);
			result = -1;
		}
		db_free_result();
	}

	return result;
}

u64_t db_get_length(unsigned row, unsigned field)
{
	if (!res) {
		trace(TRACE_WARNING, "%s,%s: result set is null\n",
		      __FILE__, __func__);
		return -1;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		trace(TRACE_ERROR, "%s, %s: "
		      "row = %u, field = %u, bigger than size of result set",
		      __FILE__, __func__, row, field);
		return -1;
	}
	
	mysql_data_seek(res, row);
	last_row = mysql_fetch_row(res);
	if (last_row == NULL) {
		trace(TRACE_ERROR, "%s,%s: last_row = NULL",
		      __FILE__, __func__);
		return (u64_t) 0;
	}
	return (u64_t) mysql_fetch_lengths(res)[field];
}

u64_t db_get_affected_rows()
{
	return (u64_t) mysql_affected_rows(&conn);
}

void db_use_msgbuf_result()
{
	stored_res = res;
	res = msgbuf_res;
}

void db_store_msgbuf_result()
{
	msgbuf_res = res;
	res = stored_res;
}

void *db_get_result_set()
{
	return (void *) res;
}

void db_set_result_set(void *the_result_set)
{
	res = (MYSQL_RES *) the_result_set;
}
