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
	}
	return NULL;
}

static MYSQL conn; /**< MySQL database connection */
static MYSQL_RES *res = NULL; /**< MySQL result set */
static MYSQL_RES *msgbuf_res = NULL; /**< MySQL result set for msgbuf */
static MYSQL_RES *stored_res = NULL; /**< MySQL result set backup */
static MYSQL_ROW last_row; /**< MySQL result row */

/* Additions by MR */

static int res_changed = 1; /* result set changed */
static unsigned last_row_number = 0; /* the number of the row in last_row */

/* database parameters */
db_param_t _db_params;

/* static functions, only used locally */

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
		return DM_EQUERY;
	}
#ifdef mysql_errno
	if (mysql_errno(&conn)) {
		trace(TRACE_ERROR, "%s,%s: mysql_real_connect failed: %s",
		      __FILE__, __func__, mysql_error(&conn));
		return DM_EQUERY;
	}
#endif
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
        res_changed = 1;
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
	
        if (res_changed) {
        	mysql_data_seek(res, row);
        	last_row = mysql_fetch_row(res);
        } else {
                if (row == last_row_number+1)
                        last_row = mysql_fetch_row(res);    
                else if (row != last_row_number) {
                        mysql_data_seek(res, row);
	                last_row = mysql_fetch_row(res);
		};
        };
	
        res_changed = 0;
        last_row_number = row;
        
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
	return DM_SUCCESS;
}

int db_check_connection()
{
	if (mysql_ping(&conn)) {
		if (db_connect() < 0) {
			trace(TRACE_ERROR, "%s,%s: unable to connect to "
			      "database.", __FILE__, __func__);
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

int db_query(const char *the_query)
{
	unsigned querysize = 0;

	if (db_check_connection() < 0) {
		trace(TRACE_ERROR, "%s,%s: no database connection",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	if (the_query != NULL) {
		querysize = (unsigned) strlen(the_query);
		if (querysize > 0) {
			trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
			      __FILE__, __func__, 
			      the_query);
			if (mysql_real_query(&conn, the_query, querysize) < 0) {
				trace(TRACE_ERROR, "%s,%s: [%s] [%s]",
				      __FILE__, __func__, 
				      mysql_error(&conn), the_query);
				return DM_EQUERY;
			}
			
			res = mysql_store_result(&conn);
			res_changed = 1;
			
		} else {
			trace(TRACE_ERROR, "%s,%s: querysize is wrong: [%d]", 
					__FILE__, __func__, querysize);
			return DM_EQUERY;
		}
	} else {
		trace(TRACE_ERROR, "%s,%s: "
		      "query buffer is NULL, this is not supposed to happen\n",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

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
			trace(TRACE_ERROR,
			      "%s,%s: error optimizing table [%s%s]",
			      __FILE__, __func__, _db_params.pfx,tables[i]);
			result = DM_EQUERY;
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
		return DM_EQUERY;
	}

	res_changed=1;

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		trace(TRACE_ERROR, "%s, %s: "
		      "row = %u, field = %u, bigger than size of result set",
		      __FILE__, __func__, row, field);
		return DM_EQUERY;
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
        res_changed = 1; /*MR*/
}

void db_store_msgbuf_result()
{
	msgbuf_res = res;
	res = stored_res;
        res_changed = 1; /*MR*/
}

void *db_get_result_set()
{
	return (void *) res;
}

void db_set_result_set(void *the_result_set)
{
	res = (MYSQL_RES *) the_result_set;
        res_changed = 1; /*MR*/
}
