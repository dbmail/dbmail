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

/**
 * dbpgsql.c
 * PostgreSQL driver file
 * Handles connection and queries to PostgreSQL backend
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "libpq-fe.h"		/* PostgreSQL header */
#include "dbmail.h"
#include "dbmailtypes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *TO_CHAR = "TO_CHAR(%s, 'YYYY-MM-DD HH24:MI:SS' )";
const char *TO_DATE = "TO_TIMESTAMP('%s', 'YYYY-MM-DD HH:MI:SS')";

static PGconn *conn;
static PGresult *res = NULL;
static PGresult *msgbuf_res;
static PGresult *stored_res;
static u64_t affected_rows; /**< stores the number of rows affected by the
			     * the last query */
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
	char connectionstring[255];

	/* use the standard port for postgresql if none is given. This looks a bit
	   dirty.. can't we get this info from somewhere else? */
	if (_db_params.port != 0)
		snprintf(connectionstring, 255,
			 "host='%s' user='%s' password='%s' dbname='%s' "
			 "port='%u'",
			 _db_params.host, _db_params.user, _db_params.pass,
			 _db_params.db, _db_params.port);
	else
		snprintf(connectionstring, 255,
			 "host='%s' user='%s' password='%s' dbname='%s' ",
			 _db_params.host, _db_params.user, _db_params.pass,
			 _db_params.db);

	conn = PQconnectdb(connectionstring);

	if (PQstatus(conn) == CONNECTION_BAD) {
		trace(TRACE_ERROR,
		      "%si,%s: PQconnectdb failed: %s",
		      __FILE__, __func__, PQerrorMessage(conn));
		return -1;
	}

	return 0;
}

int db_disconnect()
{
	if (res)
		db_free_result();
	PQfinish(conn);
	conn = NULL;

	return 0;
}

int db_check_connection()
{
	/* if there is no connection, try making one */
	if (!conn) {
		trace(TRACE_DEBUG, "%s,%s: no database connection, trying "
		      "to establish one", __FILE__, __func__);
		if (db_connect() < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: unable to connect to database",
			      __FILE__, __func__);
			return -1;
		}
		return 0;
	}

	/* check status of current connection */
	if (PQstatus(conn) != CONNECTION_OK) {
		trace(TRACE_DEBUG,
		      "%s,%s: connection lost, trying to reset", __FILE__,
		      __func__);
		PQreset(conn);

		if (PQstatus(conn) != CONNECTION_OK) {
			trace(TRACE_ERROR,
			      "%s,%s: Connection failed: [%s]", __FILE__,
			      __func__, PQerrorMessage(conn));
			trace(TRACE_ERROR,
			      "%s,%s: Could not establish dbase "
			      "connection", __FILE__, __func__);
			conn = NULL;
			return -1;
		}
	}
	return 0;
}

unsigned db_num_rows()
{
	int num_rows;
	if (!res)
		return 0;

	num_rows = PQntuples(res);
	if (num_rows < 0)
		return 0;
	else
		return (unsigned) num_rows;
}

unsigned db_num_fields()
{
	int num_fields;

	if (!res)
		return 0;
	num_fields = PQnfields(res);
	if (num_fields < 0)
		return 0;
	else
		return (unsigned) num_fields;
}

void db_free_result()
{
	if (res != NULL)
		PQclear(res);
	else
		trace(TRACE_WARNING, "%s,%s: trying to free a result set "
		      "that is already NULL!", __FILE__, __func__);
	res = NULL;
}

const char *db_get_result(unsigned row, unsigned field)
{
	if (!res) {
		trace(TRACE_WARNING, "%s,%s: result set is NULL",
		      __FILE__, __func__);
		return NULL;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		trace(TRACE_WARNING, "%s,%s: "
		      "(row = %u,field = %u) bigger then size of result set",
		      __FILE__, __func__, row, field);
		return NULL;
	}
	return PQgetvalue(res, row, field);
}

u64_t db_insert_result(const char *sequence_identifier)
{
	char query[DEF_QUERYSIZE];
	u64_t insert_result;

	/* postgres uses the currval call on a sequence to determine
	 * the result value of an insert query */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT currval('%s_seq')", sequence_identifier);

	db_query(query);
	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}
	insert_result = strtoull(db_get_result(0, 0), NULL, 10);
	db_free_result();
	return insert_result;
}

int db_query(const char *the_query)
{
	int PQresultStatusVar;
	char *result_string;
	PGresult *temp_res;
			 /**< temp_res is used as a temporary result set. If
			    the query succeeds, and needs to return a 
			    result set (i.e. it is a SELECT query)
			    the global res is 
			    set to this temp_res result set */

	if (db_check_connection() < 0) {
		trace(TRACE_ERROR, "%s,%s: No database connection",
		      __FILE__, __func__);
		return -1;
	}

	if (the_query != NULL) {
		trace(TRACE_DEBUG, "%s,%s: "
		      "executing query [%s]", __FILE__, __func__,
		      the_query);
		temp_res = PQexec(conn, the_query);
		if (!temp_res)
			return -1;

		PQresultStatusVar = PQresultStatus(temp_res);

		if (PQresultStatusVar != PGRES_COMMAND_OK &&
		    PQresultStatusVar != PGRES_TUPLES_OK) {
			trace(TRACE_ERROR, "%s, %s: "
			      "Error executing query [%s] : [%s]\n",
			      __FILE__, __func__,
			      the_query, PQresultErrorMessage(temp_res));
			PQclear(temp_res);
			return -1;
		}

		/* get the number of rows affected by the query */
		result_string = PQcmdTuples(temp_res);
		if (result_string)
			affected_rows = strtoull(result_string, NULL, 10);
		else
			affected_rows = 0;

		/* only keep the result set if this was a SELECT
		 * query */
		if (strncasecmp(the_query, "SELECT", 6) != 0) {
			if (temp_res != NULL)
				PQclear(temp_res);
		}

		else {
			if (res)
				trace(TRACE_WARNING,
				      "%s,%s: previous result set is possibly "
				      "not freed.", __FILE__,
				      __func__);
			res = temp_res;
		}
	} else {
		trace(TRACE_ERROR, "%s,%s: "
		      "query buffer is NULL, this is not supposed to happen\n",
		      __FILE__, __func__);
		return -1;
	}
	return 0;
}

unsigned long db_escape_string(char *to,
			       const char *from, unsigned long length)
{
	return PQescapeString(to, from, length);
}

int db_do_cleanup(const char **tables, int num_tables)
{
	int result = 0;
	int i;
	char the_query[DEF_QUERYSIZE];

	for (i = 0; i < num_tables; i++) {
		snprintf(the_query, DEF_QUERYSIZE, "VACUUM %s", tables[i]);
		if (db_query(the_query) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error vacuuming table [%s]",
			      __FILE__, __func__, tables[i]);
			result = -1;
		}
	}
	return result;
}

u64_t db_get_length(unsigned row, unsigned field)
{
	if (!res) {
		trace(TRACE_WARNING, "%s,%s: result set is NULL",
		      __FILE__, __func__);
		return -1;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		trace(TRACE_ERROR, "%s,%s: "
		      "(row = %u,field = %u) bigger then size of result set",
		      __FILE__, __func__, row, field);
		return -1;
	}
	return PQgetlength(res, row, field);
}

u64_t db_get_affected_rows()
{
	return affected_rows;
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
	res = (PGresult *) the_result_set;
}
