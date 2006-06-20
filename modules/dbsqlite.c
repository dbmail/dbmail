/*
  Copyright (C) 2005 Internet Connection, Inc.

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
 * dbsqlite.c
 * SQLite driver file
 * Manages access to an SQLite2/3 database
 */

#include "dbmail.h"
#include <regex.h>
#include <sqlite3.h>

db_param_t _db_params;

/* native format is the character string */

const char * db_get_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR:
			return "%s";
		break;
		case SQL_TO_DATE:
			return "'%s'";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "CURRENT_TIMESTAMP()";
		break;
		case SQL_REPLYCACHE_EXPIRE:
			return "(CURRENT_TIMESTAMP_UNIX()-%s)";	
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_REGEXP:
			return "REGEXP";
		break;
		// FIXME: To get this right, we have to supply a
		// a replacement user defined like function. According
		// to the SQLite manual, LIKE is case insensitive for
		// US-ASCII characters and case sensitive elsewhere.
		case SQL_SENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
	}
	return NULL;
}

static sqlite3 *conn;

struct qtmp {
	char **resp;
	unsigned int rows, cols;
};

struct qtmp *lastq = 0, *saveq = 0, *tempq = 0;


static void dbsqlite_current_timestamp(sqlite3_context *f, int argc UNUSED, const sqlite3_value **argv UNUSED)
{
	char timestr[21];
	struct tm tm;
	time_t now;

	time(&now);
	localtime_r(&now, &tm);
	strftime(timestr, sizeof(timestr)-1, "%Y-%m-%d %H:%M:%S", &tm);
	(void)sqlite3_result_text(f,timestr,-1,SQLITE_TRANSIENT);
}

static void dbsqlite_current_timestamp_unix(sqlite3_context *f, int argc UNUSED,  const sqlite3_value **argv UNUSED)
{
	char buf[63];
	sprintf(buf, "%ld", time(NULL)); /* assumes time() is signed int */
	(void)sqlite3_result_text(f,buf,-1,SQLITE_TRANSIENT);
}

static void dbsqlite_regexp(sqlite3_context *f, int argc, const char **argv)
{
	int res = 0;
	regex_t re;
	char *pattern, *string;

	if (argc == 2) {
		pattern = (char *)argv[0];
		string = (char *)argv[1];

		if (regcomp(&re, pattern, REG_NOSUB) == 0) {
			if (regexec(&re, string, 0, NULL, 0) == 0) {
				res = 1;
			}
			regfree(&re);
		}
	}

	(void)sqlite3_result_int(f, res);
}

int db_connect()
{
	int result;
	if ((result = sqlite3_open(_db_params.db, &conn)) != SQLITE_OK) {
		trace(TRACE_ERROR,
		      "%si,%s: sqlite3_open failed: %s",
		      __FILE__, __func__, sqlite3_errmsg(conn));
		sqlite3_close(conn);
		return -1;
	}
	if (sqlite3_create_function(conn, "CURRENT_TIMESTAMP", 0, SQLITE_ANY, NULL, (void *)dbsqlite_current_timestamp, NULL, NULL) != SQLITE_OK) {
		sqlite3_close(conn);
		trace(TRACE_ERROR, "%si,%s: sqlite3_create_function failed",
		      __FILE__, __func__);
		return -1;
	}
	if (sqlite3_create_function(conn, "CURRENT_TIMESTAMP_UNIX", 0, SQLITE_ANY, NULL, (void *)dbsqlite_current_timestamp_unix, NULL, NULL) != SQLITE_OK) {
		sqlite3_close(conn);
		trace(TRACE_ERROR, "%s,%s: sqlite3_create_function failed", __FILE__,__func__);
		return -1;
	}
	if (sqlite3_create_function(conn, "REGEXP", 2, SQLITE_ANY, NULL, (void *)dbsqlite_regexp, NULL, NULL) != SQLITE_OK) {
		sqlite3_close(conn);
		trace(TRACE_ERROR, "%s,%s: sqlite3_create_function failed", __FILE__,__func__);
		return -1;
	}
	return 0;
}

int db_check_connection()
{
	return 0;
}
void db_free_result()
{
	if (lastq) {
		if (lastq->resp) sqlite3_free_table(lastq->resp);
		lastq->resp = 0;
		lastq->rows = lastq->cols = 0;
		free(lastq);
	}
	lastq = 0;

}
int db_disconnect()
{
	db_free_result();

	db_store_msgbuf_result();
	db_free_result();

	db_use_msgbuf_result();
	db_free_result();

	sqlite3_close(conn);
	conn = 0;
	return 0;
}
unsigned db_num_rows()
{
	return lastq ? lastq->rows : 0;
}
unsigned db_num_fields()
{
	return lastq ? lastq->cols : 0;
}

const char *db_get_result(unsigned row, unsigned field)
{
	if (!lastq || !lastq->resp || !lastq->resp[row]) return NULL;
	return lastq->resp[((row+1) * lastq->cols) + field];
}

u64_t db_insert_result(const char *sequence_identifier UNUSED)
{
	if (!conn) return 0;
	return (u64_t)sqlite3_last_insert_rowid(conn);
}

int db_query(const char *the_query)
{
	char *errmsg;

	if (lastq) {
		if (lastq->resp) sqlite3_free_table(lastq->resp);
	} else {
		lastq = (struct qtmp *)malloc(sizeof(struct qtmp));
		if (!lastq) {
			trace(TRACE_ERROR,
			      "%si,%s: malloc failed: %s",
			      __FILE__, __func__, strerror(errno));
			return -1;
		}
	}
/*
	if (sqlite3_get_table(conn, the_query, &lastq->resp, (int *)&lastq->rows, (int *)&lastq->cols, &errmsg) != SQLITE_OK) {
		trace(TRACE_ERROR,
		      "%si,%s: sqlite3_exec failed: %s",
		      __FILE__, __func__, errmsg);
		sqlite3_free(errmsg);
		return -1;
	}
*/
	return 0;
}
unsigned long db_escape_string(char *to, const char *from, unsigned long length)
{
	unsigned long did = 0;
	while (length > 0) {
		if (*from == '\'') *to++ = *from;
		*to++ = *from++;
		length--;
		did++;
	}
	*to++ = *from; /* err... */
	return did;
}

unsigned long db_escape_binary(char *to, const char *from, unsigned long length)
{
	return db_escape_string(to,from,length); /* duh */
}

int db_do_cleanup(const char **tables UNUSED, int num_tables UNUSED)
{
	char *errmsg;
	if (!conn) return -1;
	if (sqlite3_exec(conn, "VACUUM", NULL, NULL, &errmsg) != SQLITE_OK) {
		trace(TRACE_ERROR,
		      "%s,%s: error vacuuming database: %s",
		      __FILE__, __func__, errmsg);
		return -1;
	}
	return 0;
}
u64_t db_get_length(unsigned row, unsigned field)
{
	const char *q = db_get_result(row,field);
	return (u64_t)strlen(q ? q : "");
}
u64_t db_get_affected_rows()
{
	if (!conn) return 0;
	return (u64_t)sqlite3_changes(conn);
}

void db_use_msgbuf_result()
{
	saveq = lastq;
	lastq = tempq;
}

void db_store_msgbuf_result()
{
	tempq = lastq;
	lastq = saveq;
}

void *db_get_result_set()
{
	return (void*)lastq;
}

void db_set_result_set(void *the_result_set)
{
	db_free_result();
	lastq = (struct qtmp *)the_result_set;
}
