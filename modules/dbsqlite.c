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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "dbmail.h"
#include "dbmailtypes.h"

#include <sqlite.h>
#include <sqlite3.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

db_param_t _db_params;

/* native format is the character string */
const char *TO_CHAR = "%s";
const char *TO_DATE = "'%s'";

const char *SQL_CURRENT_TIMESTAMP = "CURRENT_TIMESTAMP()";
const char *SQL_REPLYCACHE_EXPIRE = "DATETIME('NOW','-%s second')";

static sqlite *conn;

struct qtmp {
	char **resp;
	unsigned int rows, cols;
};

struct qtmp *lastq = 0, *saveq = 0, *tempq = 0;


static void dbsqlite_current_timestamp(sqlite_func *f, int argc UNUSED, const char **argv UNUSED)
{
	char timestr[21];
	struct tm tm;
	time_t now;

	time(&now);
	localtime_r(&now, &tm);
	strftime(timestr, sizeof(timestr)-1, "%Y-%m-%d %H:%M:%S", &tm);
	(void)sqlite_set_result_string(f,timestr,-1);
}

int db_connect()
{
	char *errmsg;
	if (!(conn = sqlite_open(_db_params.db, 0600, &errmsg))) {
		trace(TRACE_ERROR,
		      "%si,%s: sqlite_open failed: %s",
		      __FILE__, __func__, errmsg);
		sqlite_freemem(errmsg);
		return -1;
	}
	if (sqlite_create_function(conn, "CURRENT_TIMESTAMP", 0,
				dbsqlite_current_timestamp,
				0) != SQLITE_OK) {
		sqlite_close(conn);
		trace(TRACE_ERROR,
		      "%si,%s: sqlite_create_function failed",
		      __FILE__, __func__);
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
		if (lastq->resp) sqlite_free_table(lastq->resp);
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

	sqlite_close(conn);
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
	return (u64_t)sqlite_last_insert_rowid(conn);
}

int db_query(const char *the_query)
{
	char *errmsg;

	if (lastq) {
		if (lastq->resp) sqlite_free_table(lastq->resp);
	} else {
		lastq = (struct qtmp *)malloc(sizeof(struct qtmp));
		if (!lastq) {
			trace(TRACE_ERROR,
			      "%si,%s: malloc failed: %s",
			      __FILE__, __func__, strerror(errno));
			return -1;
		}
	}

	trace(TRACE_ERROR, "%s", the_query);
	if (sqlite_get_table(conn, the_query, &lastq->resp,
			&lastq->rows, &lastq->cols, &errmsg) != SQLITE_OK) {
		trace(TRACE_ERROR,
		      "%si,%s: sqlite_exec failed: %s",
		      __FILE__, __func__, errmsg);
		sqlite_freemem(errmsg);
		return -1;
	}

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
int db_do_cleanup(const char **tables UNUSED, int num_tables UNUSED)
{
	char *errmsg;
	if (!conn) return -1;
	if (sqlite_exec(conn, "VACUUM", NULL, NULL, &errmsg) != SQLITE_OK) {
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
	return (u64_t)sqlite_changes(conn);
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
