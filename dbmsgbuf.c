/*
  $Id$

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
 * \file dbmsgbuf.c
 *
 * implement msgbuf functions prototyped in dbmsgbuf.h
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmsgbuf.h"
#include "db.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MSGBUF_WINDOWSIZE (128ull*1024ull)

static unsigned _msgrow_idx = 0;
static int _msg_fetch_inited = 0;

/* for issuing queries to the backend */
char query[DEF_QUERYSIZE];

/**
 * CONDITIONS FOR MSGBUF
 */
static u64_t rowlength = 0; /**< length of current row*/
static u64_t rowpos = 0; /**< current position in row */
static db_pos_t zeropos; /**< absolute position (block/offset) of 
			    msgbuf_buf[0]*/
static unsigned nblocks = 0; /**< number of block  */

int db_init_msgfetch(u64_t msg_idnr) 
{
	u64_t physmessage_id;

	if (db_get_physmessage_id(msg_idnr, &physmessage_id) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	
	msgbuf_buf = (char *) my_malloc(sizeof(char) * (size_t) MSGBUF_WINDOWSIZE);
	if (!msgbuf_buf) {
		return -1;
	}

	if (_msg_fetch_inited != 0)
		return 0;
	memset(msgbuf_buf, '\0', (size_t) MSGBUF_WINDOWSIZE);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT messageblk FROM messageblks "
		 "WHERE physmessage_id = '%llu' ORDER BY messageblk_idnr",
		 physmessage_id);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not get message",
		      __FILE__, __FUNCTION__);
		return (-1);
	}

	nblocks = db_num_rows();

	if (nblocks == 0) {
		trace(TRACE_ERROR, "%s,%s: message has no blocks",
		      __FILE__, __FUNCTION__);
		db_free_result();

		return -1;	/* msg should have 1 block at least */
	}

	_msg_fetch_inited = 1;
	msgbuf_idx = 0;

	/* start at row (tuple) 0 */
	_msgrow_idx = 0;

	/* FIXME: this will explode is db_get_result returns NULL. */
	rowlength = db_get_length(_msgrow_idx, 0);
	strncpy(msgbuf_buf, db_get_result(_msgrow_idx, 0),
		(size_t) MSGBUF_WINDOWSIZE - 1);

	zeropos.block = 0;
	zeropos.pos = 0;

	if (rowlength >= MSGBUF_WINDOWSIZE - 1) {
		msgbuf_buflen = MSGBUF_WINDOWSIZE - 1;
		rowpos = MSGBUF_WINDOWSIZE;	/* remember store pos */
		msgbuf_buf[msgbuf_buflen] = '\0';	/* terminate buff */
		return 1;	/* msgbuf_buf full */
	}

	msgbuf_buflen = rowlength;	/* NOTE \0 has been copied from the result set */
	rowpos = rowlength;	/* no more to read from this row */

	_msgrow_idx++;
	if (_msgrow_idx >= db_num_rows()) {
		rowlength = rowpos = 0;
		return 1;
	}

	/* FIXME: this will explode is db_get_result returns NULL. */
	rowlength = db_get_length(_msgrow_idx, 0);
	rowpos = 0;
	strncpy(&msgbuf_buf[msgbuf_buflen], db_get_result(_msgrow_idx, 0),
		MSGBUF_WINDOWSIZE - msgbuf_buflen - 1);

	if (rowlength <= MSGBUF_WINDOWSIZE - msgbuf_buflen - 1) {
		/* 2nd block fits entirely */
		rowpos = rowlength;
		msgbuf_buflen += rowlength;
	} else {
		rowpos = MSGBUF_WINDOWSIZE - (msgbuf_buflen + 1);
		msgbuf_buflen = MSGBUF_WINDOWSIZE - 1;
	}
	msgbuf_buf[msgbuf_buflen] = '\0';	/* add NULL */

	/* store the current result set in db.c as msgbuf_result for
	 * later use */
	db_store_msgbuf_result();
	return 1;
}

int db_update_msgbuf(int minlen)
{
	/* use the former msgbuf_result */
	db_use_msgbuf_result();

	if (_msgrow_idx >= db_num_rows()) {
		db_store_msgbuf_result();
		return 0;	/* no more */
	}

	if (msgbuf_idx > msgbuf_buflen) {
		db_store_msgbuf_result();
		return -1;	/* error, msgbuf_idx should be within buf */
	}

	if (minlen > 0 && ((int) (msgbuf_buflen - msgbuf_idx)) > minlen) {
		db_store_msgbuf_result();
		return 1;	/* ok, need no update */
	}

	if (msgbuf_idx == 0) {
		db_store_msgbuf_result();
		return 1;	/* update no use, buffer would not change */
	}


	trace(TRACE_DEBUG,
	      "%s,%s: update msgbuf_buf updating %llu, %llu, %llu, %llu",
	      __FILE__, __FUNCTION__, MSGBUF_WINDOWSIZE,
	      msgbuf_buflen, rowlength, rowpos);

	/* move buf to make msgbuf_idx 0 */
	memmove(msgbuf_buf, &msgbuf_buf[msgbuf_idx],
		(msgbuf_buflen - msgbuf_idx));
	if (msgbuf_idx > ((msgbuf_buflen + 1) - rowpos)) {
		zeropos.block++;
		zeropos.pos = (msgbuf_idx - ((msgbuf_buflen) - rowpos));
	} else {
		zeropos.pos += msgbuf_idx;
	}

	msgbuf_buflen -= msgbuf_idx;
	msgbuf_idx = 0;

	if ((rowlength - rowpos) >= (MSGBUF_WINDOWSIZE - msgbuf_buflen)) {
		trace(TRACE_DEBUG, "%s,%s update msgbuf non-entire fit",
		      __FILE__, __FUNCTION__);

		/* rest of row does not fit entirely in buf */
		/* FIXME: this will explode is db_get_result returns NULL. */
		strncpy(&msgbuf_buf[msgbuf_buflen],
			&((db_get_result(_msgrow_idx, 0))[rowpos]),
			MSGBUF_WINDOWSIZE - msgbuf_buflen);
		rowpos += (MSGBUF_WINDOWSIZE - msgbuf_buflen - 1);

		msgbuf_buflen = MSGBUF_WINDOWSIZE - 1;
		msgbuf_buf[msgbuf_buflen] = '\0';

		db_store_msgbuf_result();
		return 1;
	}

	trace(TRACE_DEBUG, "%s,%s: update msgbuf: entire fit",
	      __FILE__, __FUNCTION__);

	/* FIXME: this will explode is db_get_result returns NULL. */
	strncpy(&msgbuf_buf[msgbuf_buflen],
		&((db_get_result(_msgrow_idx, 0))[rowpos]),
		(rowlength - rowpos));
	msgbuf_buflen += (rowlength - rowpos);
	msgbuf_buf[msgbuf_buflen] = '\0';
	rowpos = rowlength;

	/* try to fetch a new row */
	_msgrow_idx++;
	if (_msgrow_idx >= db_num_rows()) {
		trace(TRACE_DEBUG, "%s,%s update msgbuf succes NOMORE",
		      __FILE__, __FUNCTION__);
		db_store_msgbuf_result();
		return 0;
	}

	rowlength = db_get_length(_msgrow_idx, 0);
	rowpos = 0;

	trace(TRACE_DEBUG, "%s,%s: update msgbuf, got new block, "
	      "trying to place data", __FILE__, __FUNCTION__);

	/* FIXME: this will explode is db_get_result returns NULL. */
	strncpy(&msgbuf_buf[msgbuf_buflen], db_get_result(_msgrow_idx, 0),
		MSGBUF_WINDOWSIZE - msgbuf_buflen - 1);

	if (rowlength <= MSGBUF_WINDOWSIZE - msgbuf_buflen - 1) {
		/* 2nd block fits entirely */
		trace(TRACE_DEBUG,
		      "update msgbuf: new block fits entirely\n");

		rowpos = rowlength;
		msgbuf_buflen += rowlength;
	} else {
		rowpos = MSGBUF_WINDOWSIZE - (msgbuf_buflen + 1);
		msgbuf_buflen = MSGBUF_WINDOWSIZE - 1;
	}

	msgbuf_buf[msgbuf_buflen] = '\0';	/* add NULL */

	trace(TRACE_DEBUG, "%s,%s: update msgbuf succes", __FILE__,
	      __FUNCTION__);
	db_store_msgbuf_result();
	return 1;
}

void db_close_msgfetch()
{
	if (!_msg_fetch_inited)
		return;		/* nothing to be done */

	my_free(msgbuf_buf);
	msgbuf_buf = NULL;

	nblocks = 0;
	/* make sure the right result set is freed and restore the
	 * old one after that.*/
	db_use_msgbuf_result();
	db_free_result();
	db_store_msgbuf_result();
	_msg_fetch_inited = 0;
}

void db_give_msgpos(db_pos_t * pos)
{
	if (msgbuf_idx >= ((msgbuf_buflen) - rowpos)) {
		pos->block = zeropos.block + 1;
		pos->pos = msgbuf_idx - ((msgbuf_buflen) - rowpos);
	} else {
		pos->block = zeropos.block;
		pos->pos = zeropos.pos + msgbuf_idx;
	}
}

u64_t db_give_range_size(db_pos_t * start, db_pos_t * end)
{
	unsigned i;
	u64_t size;

	if (start->block > end->block)
		return 0;	/* bad range */

	if (start->block >= nblocks || end->block >= nblocks)
		return 0;	/* bad range */

	if (start->block == end->block)
		return (start->pos >
			end->pos) ? 0 : (end->pos - start->pos + 1);

	/* use the former msgbuf result */
	db_use_msgbuf_result();

	if (start->pos > db_get_length(start->block, 0) ||
	    end->pos > db_get_length(end->block, 0)) {
		db_store_msgbuf_result();
		return 0;	/* bad range */
	}

	size = db_get_length(start->block, 0) - start->pos;

	for (i = start->block + 1; i < end->block; i++)
		size += db_get_length(i, 0);

	size += end->pos;
	size++;

	/* store the result again.. */
	db_store_msgbuf_result();
	return size;
}

long db_dump_range(MEM * outmem, db_pos_t start,
		   db_pos_t end, u64_t msg_idnr)
{
	u64_t physmessage_id;
	u64_t i, startpos, endpos, j, bufcnt;
	u64_t outcnt;
	u64_t distance;
	char buf[DUMP_BUF_SIZE];
	const char *field;

	if (db_get_physmessage_id(msg_idnr, &physmessage_id) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	trace(TRACE_DEBUG,
	      "%s,%s: Dumping range: (%llu,%llu) - (%llu,%llu)",
	      __FILE__, __FUNCTION__,
	      start.block, start.pos, end.block, end.pos);

	if (start.block > end.block) {
		trace(TRACE_ERROR, "%s,%s: bad range specified",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (start.block == end.block && start.pos > end.pos) {
		trace(TRACE_ERROR, "%s,%s: bad range specified",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT messageblk FROM messageblks "
		 "WHERE physmessage_id = '%llu' "
		 "ORDER BY messageblk_idnr", physmessage_id);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not get message",
		      __FILE__, __FUNCTION__);
		return (-1);
	}

	if (start.block >= db_num_rows()) {
		trace(TRACE_ERROR,
		      "db_dump_range(): bad range specified\n");
		db_free_result();
		return -1;
	}

	outcnt = 0;

	/* just one block? */
	if (start.block == end.block) {
		/* dump everything */
		bufcnt = 0;
		field = db_get_result(start.block, 0);

		for (i = start.pos; i <= end.pos; i++) {
			if (bufcnt >= DUMP_BUF_SIZE - 1) {
				outcnt += mwrite(buf, bufcnt, outmem);
				bufcnt = 0;
			}

			/* FIXME: field may be NULL from db_get_result! */
			if (field[i] == '\n' &&
			    !(i > 0 && field[i - 1] == '\r')) {
				trace(TRACE_DEBUG,
				      "%s,%s: adding '\r' to buf",
				      __FILE__, __FUNCTION__);
				buf[bufcnt++] = '\r';
				buf[bufcnt++] = '\n';
			} else
				buf[bufcnt++] = field[i];
		}

		outcnt += mwrite(buf, bufcnt, outmem);
		bufcnt = 0;

		db_free_result();
		return outcnt;
	}


	/* 
	 * multiple block range specified
	 */

	for (i = start.block, outcnt = 0; i <= end.block; i++) {
		if (i >= db_num_rows()) {
			trace(TRACE_ERROR,
			      "db_dump_range(): bad range specified\n");
			db_free_result();
			return -1;
		}

		startpos = (i == start.block) ? start.pos : 0;
		endpos =
		    (i == end.block) ? end.pos + 1 : db_get_length(i, 0);

		distance = endpos - startpos;

		/* output */
		bufcnt = 0;
		field = db_get_result(i, 0);

		for (j = 0; j < distance; j++) {
			if (bufcnt >= DUMP_BUF_SIZE - 1) {
				outcnt += mwrite(buf, bufcnt, outmem);
				bufcnt = 0;
			}

			/* FIXME: field may be NULL from db_get_result! */
			if (field[startpos + j] == '\n' &&
			    !(j > 0 && field[startpos + j - 1] == '\r')) {
				trace(TRACE_DEBUG,
				      "%s,%s: adding '\r' to buf",
				      __FILE__, __FUNCTION__);

				buf[bufcnt++] = '\r';
				buf[bufcnt++] = '\n';
			} else if (field[startpos + j])
				buf[bufcnt++] = field[startpos + j];
		}
		outcnt += mwrite(buf, bufcnt, outmem);
		bufcnt = 0;
	}

	db_free_result();

	return outcnt;
}
