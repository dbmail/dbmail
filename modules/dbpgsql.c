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

#define THIS_MODULE "pgsql"
#define BYTEAOID  17

const char * db_get_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "TO_CHAR(%s, 'YYYY-MM-DD HH24:MI:SS' )";
		break;
		case SQL_TO_DATE:
			return "TO_TIMESTAMP('%s', 'YYYY-MM-DD HH:MI:SS')";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "CURRENT_TIMESTAMP";
		break;
		case SQL_REPLYCACHE_EXPIRE:
			return "NOW() - INTERVAL '%d DAY'";
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_REGEXP:
			return "~";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "ILIKE";
		break;
		case SQL_ENCODE_ESCAPE:
			return "ENCODE(%s::bytea,'escape')";
		break;
	}
	return NULL;
}

static PGconn *conn = NULL;
static PGresult *res = NULL;
static void _create_binary_table(void);
static void _free_binary_table(void);
static void _set_binary_table(unsigned row, unsigned field);
static char*** bintbl = NULL;
db_param_t _db_params;

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
		TRACE(TRACE_ERROR, "PQconnectdb failed: %s", PQerrorMessage(conn));
		return -1;
	}
#ifdef OLD
	/* UNICODE is broken prior to 8.1 */
	if (PQserverVersion(conn) < 80100) {
		char *enc = NULL;

		enc = pg_encoding_to_char(PQclientEncoding(conn));
		// if (strcmp(enc, "SQL_ASCII") != 0) {
		if (strcmp(enc, "UNICODE") == 0) {
			trace(TRACE_FATAL, "%s,%s: Database encoding UNICODE"
				"is not supported prior to PostgreSQL 8.1",
				__FILE__, __func__);
		}

		// FIXME: Does we need to free enc?
	}
#endif
	return 0;
}

int db_check_connection() {
	if (!conn) {
		/* There seem to be some circumstances which cause
		 * db_check_connection to be called before db_connect. */
		TRACE(TRACE_ERROR, "connection with database invalid, retrying");
		return db_connect();
	}

	if (PQstatus(conn) == CONNECTION_BAD) {
		PQreset(conn);
		if (PQstatus(conn) == CONNECTION_BAD) {
			TRACE(TRACE_ERROR, "connection with database gone bad");
			return -1;
		}
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
	_free_binary_table();
	if (res != NULL)
		PQclear(res);
	res = NULL;
}

static void _create_binary_table(void){
	unsigned rows, fields, i;
	rows = db_num_rows(); fields = db_num_fields();
	
	if(!bintbl){
		bintbl = (char***)g_malloc(sizeof(char**) * rows);
		memset(bintbl, 0, sizeof(char**) * rows);
		for(i = 0; i < rows; i++){
			*(bintbl + i) = (char**)g_malloc(sizeof(char*) * fields);
			memset(*(bintbl + i), 0, sizeof(char*) * fields);
		}
	}
}

static void _free_binary_table(void){
	unsigned rows, fields, i, j;
	rows = db_num_rows(); fields = db_num_fields();

	if(bintbl){
		for(i = 0; i < rows; i++){
			for(j = 0; j < fields; j++)
				if(bintbl[i][j])
					g_free(bintbl[i][j]);
			g_free(bintbl[i]);
		}
		g_free(bintbl);
		bintbl = NULL;
	}
	
}
static void _set_binary_table(unsigned row, unsigned field){
	unsigned char* tmp;
	size_t result_size;
	if(!bintbl[row][field]){
		tmp = PQunescapeBytea((const unsigned char *)PQgetvalue(res, row, field), &result_size);
		bintbl[row][field] = (char*)g_malloc(result_size + 1);
		memcpy(bintbl[row][field], tmp, result_size);
		PQfreemem(tmp); tmp = NULL;
		bintbl[row][field][result_size] = '\0';
	}
}

const char *db_get_result(unsigned row, unsigned field)
{
	if (!res) {
		TRACE(TRACE_WARNING, "result set is NULL");
		return NULL;
	}

	if ((row > db_num_rows()) || (field > db_num_fields())) {
		TRACE(TRACE_WARNING, "row = %u or field = %u out of range", row, field);
		return NULL;
	}
	if(PQftype(res, field) == BYTEAOID){
		_create_binary_table();
		_set_binary_table(row, field);
		return bintbl[row][field];	
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
		 "SELECT currval('%s%s_seq')",_db_params.pfx, sequence_identifier);

	db_query(query);
	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}
	insert_result = strtoull(db_get_result(0, 0), NULL, 10);
	db_free_result();
	return insert_result;
}

int db_query(const char *q)
{
	int PQresultStatusVar;
	char *result_string;
	PGresult *temp_res;
	
	db_free_result();

	g_return_val_if_fail(q != NULL,DM_EQUERY);

	if (db_check_connection())
		return DM_EQUERY;

	TRACE(TRACE_DEBUG, "[%s]", q);

	if (! (res = PQexec(conn, q)))
		return DM_EQUERY;
	
	PQresultStatusVar = PQresultStatus(res);

	if (PQresultStatusVar != PGRES_COMMAND_OK && PQresultStatusVar != PGRES_TUPLES_OK) {
		TRACE(TRACE_ERROR, "query failed [%s] : [%s]\n", q, PQresultErrorMessage(res));
		db_free_result();
		return DM_EQUERY;
	}

	return 0;
}

unsigned long db_escape_string(char *to,
			       const char *from, unsigned long length)
{
	return PQescapeString(to, from, length);
}
unsigned long db_escape_binary(char *to,
			       const char *from, unsigned long length)
{
	size_t to_length;
	unsigned char *esc_to;

	esc_to = PQescapeBytea((const unsigned char *)from, length, &to_length);
	strncpy(to, (const char *)esc_to, to_length);
	PQfreemem(esc_to);
	return (unsigned long)(to_length - 1);
}
int db_do_cleanup(const char **tables, int num_tables)
{
	int result = 0;
	int i;
	char the_query[DEF_QUERYSIZE];

	for (i = 0; i < num_tables; i++) {
		snprintf(the_query, DEF_QUERYSIZE, "VACUUM %s%s", _db_params.pfx,tables[i]);
		if (db_query(the_query) == -1) {
			TRACE(TRACE_ERROR, "error vacuuming table [%s%s]", 
				_db_params.pfx,tables[i]);
			result = -1;
		}
	}
	return result;
}

u64_t db_get_length(unsigned row, unsigned field)
{
	if (!res) {
		TRACE(TRACE_WARNING, "result set is NULL");
		return -1;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		TRACE(TRACE_ERROR, "row = %u or field = %u out of range", row, field);
		return -1;
	}
        if(PQftype(res, field) == BYTEAOID){
                _create_binary_table();
                _set_binary_table(row, field);
		return strlen(bintbl[row][field]);
        }
	return PQgetlength(res, row, field);
}

u64_t db_get_affected_rows()

{
	char *s;
	if (! res) return 0;
	if ((s = PQcmdTuples(res)) != NULL)
		return strtoull(s, NULL, 10);
	return 0;
}

void *db_get_result_set()
{
	return (void *) res;
}

void db_set_result_set(void *the_result_set)
{
	res = (PGresult *) the_result_set;
}
