/*
  Copyright (C) 2006 DBMail.EU support@dbmail.eu

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
 * dbingres.c
 * Ingres2006 driver file
 * Handles connection and queries to Ingres backend
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "iiapi.h"		/* Ingres header */
#include "dbmail.h"
#include "dbmailtypes.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define THIS_MODULE "sql"

const char * db_get_sql(sql_fragment_t frag)
{
	switch(frag) {
		case SQL_TO_CHAR: //FIXME??
			return "%s";
		break;
		case SQL_TO_DATE:
			return "DATE('%s')";
		break;
		case SQL_CURRENT_TIMESTAMP:
			return "DATE('NOW')";
		break;
		case SQL_REPLYCACHE_EXPIRE:
			return "DATE('NOW') - '%d DAY'";
		break;
		case SQL_BINARY:
			return "";
		break;
		case SQL_SENSITIVE_LIKE:
			return "LIKE";
		break;
		case SQL_INSENSITIVE_LIKE: //FIXME
			return "LIKE";
		break;
		case SQL_ENCODE_ESCAPE: //FIXME?
			return "'%s'";
		break;
		default:
			return "";
		break;

	}
	return NULL;
}

static II_PTR		conn		= (II_PTR)NULL;
static II_PTR		envhandle	= (II_PTR)NULL;
static II_PTR		res		= (II_PTR)NULL; // statement (result) handler
static II_PTR		trn		= (II_PTR)NULL; // transaction handler
static IIAPI_GETCOLPARM	cparm;
static gboolean		_in_transaction = FALSE;
 
db_param_t _db_params;

static IIAPI_STATUS _db_wait(IIAPI_GENPARM *gp){
	IIAPI_WAITPARM wt;

	for( wt.wt_timeout = -1 ; ! gp->gp_completed ; )
		IIapi_wait(&wt);
	return gp->gp_status;
}


static IIAPI_GETDESCRPARM get_getDescriptor(void)
{
	IIAPI_GETDESCRPARM gd;
	IIAPI_STATUS status;

	g_return_val_if_fail(res != NULL, gd);

	gd.gd_genParm.gp_callback = NULL;
	gd.gd_genParm.gp_closure = NULL;
	gd.gd_stmtHandle = res;
	gd.gd_descriptorCount = 0;
	gd.gd_descriptor = NULL;

	IIapi_getDescriptor( &gd );

	status = _db_wait (&gd.gd_genParm);
	if (status >= IIAPI_ST_ERROR)
		TRACE(TRACE_ERROR, "failed");

	return gd;
}

static void get_all_results(void) 
{
	unsigned col, row;
	unsigned rowsleft = 0;
	unsigned r,c;
	IIAPI_GETDESCRPARM 	dp;
	IIAPI_DATAVALUE		*DataBuffer;
	IIAPI_STATUS status;

	if (cparm.gc_columnData != NULL)
		return;

	dp = get_getDescriptor();
	col = db_num_fields();
	row = db_num_rows();

	if (row < 1 || col < 1)
		return;

	DataBuffer = g_new0(IIAPI_DATAVALUE, (row * col)+1);

	cparm.gc_genParm.gp_callback = NULL;
	cparm.gc_genParm.gp_closure = NULL;
	cparm.gc_moreSegments = 0;
	cparm.gc_columnData = DataBuffer;
	cparm.gc_stmtHandle = res;
	cparm.gc_columnCount = col;
	cparm.gc_rowCount = 1;

	for (r=0; r<row; r++) {
		for (c=0; c<col; c++) {
			cparm.gc_columnData[(r*col)+c].dv_value = g_new0(char,dp.gd_descriptor[c].ds_length);
		}
	}

	cparm.gc_columnData[row*col].dv_value = NULL;

	rowsleft = row;
	do {
		// FIXME: we can do better here if there's no 
		// IIAPI_LBYTE_TYPE or IIAPI_LVCH_TYPE being 
		// returned
		//
		//cparm.gc_rowCount = rowsleft;

		IIapi_getColumns( &cparm );

		status = _db_wait(&cparm.gc_genParm);
		if (status < IIAPI_ST_ERROR)
			rowsleft = row - cparm.gc_rowsReturned;
		else {
			TRACE(TRACE_ERROR, "failed");
			break;
		}

	} while (cparm.gc_moreSegments || rowsleft > 0);


}


static void db_init(II_PTR *envhandle)
{
	IIAPI_INITPARM initparm;

	if (*envhandle)
		return;

	initparm.in_version = IIAPI_VERSION_2;
	initparm.in_timeout = -1;
	IIapi_initialize(&initparm);
	*envhandle = initparm.in_envHandle;
	return;
}

static void db_term(II_PTR *envhandle)
{

	IIAPI_RELENVPARM	relenvparm;
	IIAPI_TERMPARM		termparm;

	relenvparm.re_envHandle = *envhandle;
	IIapi_releaseEnv( &relenvparm );
	IIapi_terminate( &termparm );
	
	*envhandle = NULL;
}	

static void _db_autocommit(void)
{
	IIAPI_AUTOPARM ac;
	IIAPI_STATUS status;
	gboolean ac_state = FALSE;

	if (trn)
		ac_state = TRUE;


	ac.ac_genParm.gp_callback = NULL;
	ac.ac_genParm.gp_closure = NULL;
	ac.ac_connHandle = conn;
	ac.ac_tranHandle = trn;

	IIapi_autocommit( &ac );

	status = _db_wait(&ac.ac_genParm);
	if (status < IIAPI_ST_ERROR) {
		TRACE(TRACE_DEBUG,"%s", ac_state ? "disabled" : "enabled");
		if (ac_state)
			trn = NULL;
		else
			trn = ac.ac_tranHandle;
	} else {
		TRACE(TRACE_ERROR,"toggle failed");
	}
	return;
}

int db_connect(void)
{
	gchar *target;
	IIAPI_CONNPARM	co;
	IIAPI_STATUS	status;

	target = g_strdup_printf("%s::%s", _db_params.host, _db_params.db);

	db_init(&envhandle);

	co.co_genParm.gp_callback = NULL;
	co.co_genParm.gp_closure = NULL;
	co.co_target = target;
	co.co_connHandle = conn;
	co.co_tranHandle = NULL;
	co.co_username = _db_params.user;
	co.co_password = _db_params.pass;
	co.co_timeout = -1;
	
	IIapi_connect ( &co );

	status = _db_wait(&co.co_genParm);
	if (status < IIAPI_ST_ERROR) {
		conn = co.co_connHandle;
		_db_autocommit();
	} else {
		TRACE(TRACE_ERROR, "failed");
		db_disconnect();
		return -1;
	}

	return 0;
}

int db_check_connection() {
	if (!conn)
		return db_connect();

	// TODO
	
	return 0;
}

int db_disconnect()
{
	IIAPI_DISCONNPARM	dc;
	IIAPI_STATUS		status;
	int result = 0;

	if (res)
		db_free_result();

	dc.dc_genParm.gp_callback = NULL;
	dc.dc_genParm.gp_closure = NULL;
	dc.dc_connHandle = conn;

	IIapi_disconnect ( &dc );
	
	status = _db_wait(&dc.dc_genParm);

	if (status >= IIAPI_ST_ERROR) {
		TRACE(TRACE_ERROR, "failed");
		result = -1;
	}
	
	db_term(&envhandle);
	return result;
}

static int _db_commit(II_PTR tran)
{
	IIAPI_COMMITPARM	cm;
	IIAPI_CLOSEPARM		cl;
	IIAPI_STATUS		status;
	int result = 0;

	if (! tran)
		return -1;

	TRACE(TRACE_DEBUG, "commit transaction");

	cm.cm_genParm.gp_callback = NULL;
	cm.cm_genParm.gp_closure = NULL;
	cm.cm_tranHandle = tran;

	IIapi_commit ( &cm );

	status = _db_wait(&cm.cm_genParm);

	if (status >= IIAPI_ST_ERROR) {
		TRACE(TRACE_ERROR, "commit failed");
		result = -1;
	}

	cl.cl_genParm.gp_callback = NULL;
	cl.cl_genParm.gp_closure = NULL;
	cl.cl_stmtHandle = tran;

	IIapi_close( &cl );

	status = _db_wait(&cl.cl_genParm);
	if (status >= IIAPI_ST_ERROR) {
		TRACE(TRACE_ERROR, "close failed");
		result = -1;
	}

	return result;
}

int db_query(const char *q)
{
	IIAPI_QUERYPARM		qp;
	IIAPI_STATUS		status;
	int result = 0;

	db_free_result();

	g_return_val_if_fail(q != NULL,DM_EQUERY);

	if (db_check_connection())
		return DM_EQUERY;

	TRACE(TRACE_DEBUG, "[%s]", q);

	if (strncasecmp("BEGIN",q,5)==0) {
		_in_transaction = 1;
		_db_autocommit(); // turn off autocommit;
		return 0;
	}
	if (strncasecmp("COMMIT",q,6)==0) {
		_db_commit(trn);
		_in_transaction = 0;
		_db_autocommit(); // turn on autocommit;
		trn = NULL;
		return 0;
	}

	qp.qy_genParm.gp_callback = NULL;
	qp.qy_genParm.gp_closure = NULL;
	qp.qy_connHandle = conn;
	qp.qy_queryType = IIAPI_QT_QUERY;
	qp.qy_queryText = (char *)q;
	qp.qy_parameters = FALSE;
	qp.qy_tranHandle = trn;
	qp.qy_stmtHandle = NULL;

	IIapi_query ( &qp );

	status = _db_wait(&qp.qy_genParm);

	if (status >= IIAPI_ST_ERROR) {
		TRACE(TRACE_ERROR, "failed");
		result = -1;
	}

	res = qp.qy_stmtHandle;

	if (_in_transaction) {
		trn = res;
	} else {
		_db_commit(trn);
		trn = NULL;
	}

	return result;
}


unsigned db_num_rows()
{
	IIAPI_GETQINFOPARM	gq;
	IIAPI_STATUS		status;

	if (!res)
		return 0;
	
	gq.gq_genParm.gp_callback = NULL;
	gq.gq_genParm.gp_closure = NULL;
	gq.gq_stmtHandle = res;
	gq.gq_mask = IIAPI_GQ_ROW_COUNT;

	IIapi_getQueryInfo ( &gq );

	status = _db_wait (&gq.gq_genParm);
	if (status >= IIAPI_ST_ERROR) {
		TRACE(TRACE_ERROR, "failed");
		return 0;
	}

	return (unsigned)gq.gq_rowCount;
}

unsigned db_num_fields()
{
	IIAPI_GETDESCRPARM	gd;

	if (!res)
		return 0;

	gd = get_getDescriptor();

	return (unsigned)gd.gd_descriptorCount;
}

void db_free_result()
{
	II_LONG			f = 0;
	IIAPI_CLOSEPARM		cl;
	IIAPI_STATUS		status;

	if (! res)
		return;

	cl.cl_genParm.gp_callback = NULL;
	cl.cl_genParm.gp_closure = NULL;
	cl.cl_stmtHandle = res;

	IIapi_close( &cl );

	status = _db_wait(&cl.cl_genParm);
	if (status >= IIAPI_ST_ERROR)
		TRACE(TRACE_ERROR, "close failed");

	res = NULL;
	if (cparm.gc_columnData == NULL)
		return;

	while (cparm.gc_columnData[f].dv_value)
		g_free(cparm.gc_columnData[f++].dv_value);

	g_free(cparm.gc_columnData);
	cparm.gc_columnData = NULL;

	return;
}
const char *db_get_result(unsigned row, unsigned field)
{
	unsigned rows, fields;

	if (!res) {
		TRACE(TRACE_WARNING, "result set is NULL");
		return NULL;
	}

	rows = db_num_rows();
	fields = db_num_fields();

	if ((row > rows) || (field > fields)) {
		TRACE(TRACE_WARNING, "row = %u or field = %u out of range", row, field);
		return NULL;
	}
	
	get_all_results();

	return cparm.gc_columnData[row*fields+field].dv_value;

}

u64_t db_insert_result(const char *seq_id)
{
	return db_sequence_currval(seq_id);
}

u64_t db_sequence_currval(const char *seq_id)
{
	char query[DEF_QUERYSIZE];
	u64_t seq;

	snprintf(query, DEF_QUERYSIZE, "SELECT %s%s_seq.currval",_db_params.pfx, seq_id);

	db_query(query);
	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}
	seq = strtoull(db_get_result(0, 0), NULL, 10);
	db_free_result();

	TRACE(TRACE_DEBUG, "return-value: %llu", seq);

	return seq;
}

u64_t db_sequence_nextval(const char *seq_id)
{
	char query[DEF_QUERYSIZE];
	u64_t seq;

	snprintf(query, DEF_QUERYSIZE, "SELECT %s%s_seq.nextval",_db_params.pfx, seq_id);

	db_query(query);
	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}
	seq = strtoull(db_get_result(0, 0), NULL, 10);
	db_free_result();

	TRACE(TRACE_DEBUG, "return-value: %llu", seq);

	return seq;
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

unsigned long db_escape_binary(char *to,
			       const char *from, unsigned long length)
{
	static char *hex = "0123456789ABCDEF";
	const char *q;
	*to++ = 'X';
	*to++ = '\'';
	for(q = from; length--;){
		*to++ = hex[(*q) >> 4];
		*to++ = hex[(*q) & 0xf];
		++q;
	}
	*to++ = '\'';
	*to = '\0';
	return length*2+4;
}

int db_do_cleanup(const char UNUSED **tables, int UNUSED num_tables)
{
	int result = 0;
	// FIXME: does ingres need this?
	return result;
}

u64_t db_get_length(unsigned UNUSED row, unsigned field)
{
	IIAPI_GETDESCRPARM	dp;

	if (!res) {
		TRACE(TRACE_WARNING, "result set is NULL");
		return 0;
	}

	if ((row >= db_num_rows()) || (field >= db_num_fields())) {
		TRACE(TRACE_ERROR, "row = %u or field = %u out of range", row, field);
		return 0;
	}

	dp = get_getDescriptor();
	return (u64_t)dp.gd_descriptor[field].ds_length;
}

u64_t db_get_affected_rows()
{
	return db_num_rows();
}

void *db_get_result_set()
{
	return (void *) res;
}

void db_set_result_set(void *the_result_set)
{
	res = (II_PTR)the_result_set;
}
