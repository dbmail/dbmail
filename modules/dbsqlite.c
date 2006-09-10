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

#define THIS_MODULE "dbsqlite"

db_param_t _db_params;

static sqlite3 *conn;

/* SQLITE3 internals... */
extern int sqlite3ReadUtf8(const unsigned char *);
extern const unsigned char sqlite3UpperToLower[];
extern int sqlite3utf8CharLen(const char *pData, int nByte);

const char * db_get_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_ENCODE_ESCAPE:
		case SQL_TO_CHAR:
			return "%s";
		break;
		case SQL_TO_DATE:
			return "'%s'";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "STRFTIME('%Y-%m-%d %H:%M:%S','now','localtime')";
		break;
		case SQL_REPLYCACHE_EXPIRE:
			return "(STRFTIME('%s','now')-%s)";	
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_REGEXP:
			trace(TRACE_ERROR, "We deliberately don't support REGEXP operations.");
			sqlite3_close(conn);
			exit(255);
		break;
		/* some explaining:
		 *
		 * sqlite3 has a limited number of A x B operators: LIKE, GLOB, REGEXP.
		 * we need two until this function knows how to go %s LIKE %s so that we can
		 * instead use LIKE_INSENSITIVE(%s,%s) or whatnot.
		 *
		 * until then....
		 */
		case SQL_SENSITIVE_LIKE:
			return "REGEXP";
		break;
		case SQL_INSENSITIVE_LIKE:
			return "LIKE";
		break;
	}
	return NULL;
}

struct qtmp {
	char **resp;
	int rows, cols;
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

/* this is lifted directly from sqlite -- cut here -- */
struct compareInfo {
	unsigned char matchAll;
	unsigned char matchOne;
	unsigned char matchSet;
	unsigned char noCase;
};
static const struct compareInfo likeInfo = { '%', '_',   0, 0 };
#define sqliteNextChar(X)  while( (0xc0&*++(X))==0x80 ){}
#define sqliteCharVal(X)   sqlite3ReadUtf8(X)

static int patternCompare(
  const unsigned char *zPattern,              /* The glob pattern */
  const unsigned char *zString,               /* The string to compare against the glob */
  const struct compareInfo *pInfo, /* Information about how to do the compare */
  const int esc                    /* The escape character */
){
  register int c;
  int invert;
  int seen;
  int c2;
  unsigned char matchOne = pInfo->matchOne;
  unsigned char matchAll = pInfo->matchAll;
  unsigned char matchSet = pInfo->matchSet;
  unsigned char noCase = pInfo->noCase; 
  int prevEscape = 0;     /* True if the previous character was 'escape' */

  while( (c = *zPattern)!=0 ){
    if( !prevEscape && c==matchAll ){
      while( (c=zPattern[1]) == matchAll || c == matchOne ){
        if( c==matchOne ){
          if( *zString==0 ) return 0;
          sqliteNextChar(zString);
        }
        zPattern++;
      }
      if( c && esc && sqlite3ReadUtf8(&zPattern[1])==esc ){
        unsigned char const *zTemp = &zPattern[1];
        sqliteNextChar(zTemp);
        c = *zTemp;
      }
      if( c==0 ) return 1;
      if( c==matchSet ){
        assert( esc==0 );   /* This is GLOB, not LIKE */
        while( *zString && patternCompare(&zPattern[1],zString,pInfo,esc)==0 ){
          sqliteNextChar(zString);
        }
        return *zString!=0;
      }else{
        while( (c2 = *zString)!=0 ){
          if( noCase ){
            c2 = sqlite3UpperToLower[c2];
            c = sqlite3UpperToLower[c];
            while( c2 != 0 && c2 != c ){ c2 = sqlite3UpperToLower[*++zString]; }
          }else{
            while( c2 != 0 && c2 != c ){ c2 = *++zString; }
          }
          if( c2==0 ) return 0;
          if( patternCompare(&zPattern[1],zString,pInfo,esc) ) return 1;
          sqliteNextChar(zString);
        }
        return 0;
      }
    }else if( !prevEscape && c==matchOne ){
      if( *zString==0 ) return 0;
      sqliteNextChar(zString);
      zPattern++;
    }else if( c==matchSet ){
      int prior_c = 0;
      assert( esc==0 );    /* This only occurs for GLOB, not LIKE */
      seen = 0;
      invert = 0;
      c = sqliteCharVal(zString);
      if( c==0 ) return 0;
      c2 = *++zPattern;
      if( c2=='^' ){ invert = 1; c2 = *++zPattern; }
      if( c2==']' ){
        if( c==']' ) seen = 1;
        c2 = *++zPattern;
      }
      while( (c2 = sqliteCharVal(zPattern))!=0 && c2!=']' ){
        if( c2=='-' && zPattern[1]!=']' && zPattern[1]!=0 && prior_c>0 ){
          zPattern++;
          c2 = sqliteCharVal(zPattern);
          if( c>=prior_c && c<=c2 ) seen = 1;
          prior_c = 0;
        }else if( c==c2 ){
          seen = 1;
          prior_c = c2;
        }else{
          prior_c = c2;
        }
        sqliteNextChar(zPattern);
      }
      if( c2==0 || (seen ^ invert)==0 ) return 0;
      sqliteNextChar(zString);
      zPattern++;
    }else if( esc && !prevEscape && sqlite3ReadUtf8(zPattern)==esc){
      prevEscape = 1;
      sqliteNextChar(zPattern);
    }else{
      if( noCase ){
        if( sqlite3UpperToLower[c] != sqlite3UpperToLower[*zString] ) return 0;
      }else{
        if( c != *zString ) return 0;
      }
      zPattern++;
      zString++;
      prevEscape = 0;
    }
  }
  return *zString==0;
}
/* this is lifted directly from sqlite -- cut here -- */
/* this is lifted "almost" :) directly from sqlite -- cut here -- */
static void dbsqlite_cslike(sqlite3_context *context, int argc, sqlite3_value **argv)
{
	/* this code comes from SQLite's built-in LIKE function */
	const unsigned char *zA = sqlite3_value_text(argv[0]);
	const unsigned char *zB = sqlite3_value_text(argv[1]);
	int escape = 0;
	if( argc==3 ){
		/* The escape character string must consist of a single UTF-8 character.
		** Otherwise, return an error.
		*/
		const unsigned char *zEsc = sqlite3_value_text(argv[2]);
		if( sqlite3utf8CharLen(zEsc, -1)!=1 ){
			sqlite3_result_error(context, 
				"ESCAPE expression must be a single character", -1);
			return;
		}
		escape = sqlite3ReadUtf8(zEsc);
	}
	if( zA && zB ){
		sqlite3_result_int(context, patternCompare(zA, zB, &likeInfo, escape));
	}
}
/* this is lifted "almost" :) directly from sqlite -- cut here -- */

int db_connect()
{
	int result;
	if ((result = sqlite3_open(_db_params.db, &conn)) != SQLITE_OK) {
		trace(TRACE_FATAL, "%s,%s: sqlite3_open failed: %s",
		      __FILE__, __func__, sqlite3_errmsg(conn));
		sqlite3_close(conn);
		return -1;
	}
	if (sqlite3_create_function(conn, "REGEXP", 2, SQLITE_ANY, NULL, (void *)dbsqlite_cslike, NULL, NULL) != SQLITE_OK) {
		sqlite3_close(conn);
		trace(TRACE_FATAL, "%s,%s: sqlite3_create_function failed", __FILE__,__func__);
		return -1;
	}

	lastq = g_new0(struct qtmp,1);

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
	}
}
int db_disconnect()
{
	db_free_result();
	sqlite3_close(conn);
	g_free(lastq);
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

	trace(TRACE_DEBUG,"%s,%s: %s", __FILE__, __func__, the_query);

	db_free_result();

	if (sqlite3_get_table(conn, the_query, &lastq->resp,
				(int *)&lastq->rows, (int *)&lastq->cols, &errmsg) != SQLITE_OK) {
		trace(TRACE_ERROR,
		      "%si,%s: sqlite3_get_table failed: %s",
		      __FILE__, __func__, errmsg);
		sqlite3_free(errmsg);
		return -1;
	}
	if (lastq->rows < 0 || lastq->cols < 0) {
		lastq->rows = 0;
		lastq->cols = 0;
	}
	return 0;
}
unsigned long db_escape_string(char *to, const char *from, unsigned long length)
{
	unsigned long did = 0;
	while (*from && did < length) {
		if (*from == '\'')
			*to++ = *from;
		*to++ = *from++;
		did++;
	}
	*to++ = '\0';
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
	/* while VACUUM doesn't do anything when PRAGMA auto_vacuum=1
	 * (which happens to be the new default), it also doesn't hurt
	 * either...
	 */
	if (sqlite3_exec(conn, "VACUUM", NULL, NULL, &errmsg) != SQLITE_OK) {
		/* no reporting... */
		sqlite3_free(errmsg);
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
