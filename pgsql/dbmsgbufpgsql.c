/* 
 * $Id$
 *
 * function implementations for the msgbuf system
 * using a pgsql database
 *
 * (c) 2000-2002 IC&S (http://www.ic-s.nl)
 */

#include "../dbmsgbuf.h"
#include "../db.h"
#include "/usr/local/pgsql/include/libpq-fe.h"
/*#include "/usr/include/postgresql/libpq-fe.h"*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MSGBUF_WINDOWSIZE (128ul*1024ul)

/* 
 * var's from dbpgsql.c
 */

extern PGconn *conn;  
extern PGresult *res;
extern PGresult *checkres;
extern char *query;
extern char *value; /* used for PQgetvalue */
extern unsigned long PQcounter; /* used for PQgetvalue loops */

/* own var's */
PGresult *_msg_result;
unsigned _msgrow_idx = 0;
int _msg_fetch_inited = 0;

/*
 * CONDITIONS FOR MSGBUF
 *
 * rowlength         length of current row
 * rowpos            current pos in row 
 * msgidx            index within msgbuf, 0 <= msgidx < buflen
 * buflen            current buffer length: msgbuf[buflen] == '\0'
 * zeropos           absolute position (block/offset) of msgbuf[0]
 */

char *msgbuf;
u64_t rowlength = 0,msgidx=0,buflen=0,rowpos=0;
db_pos_t zeropos;
unsigned nblocks = 0;


/*
 * db_init_msgfetch()
 *  
 * initializes a msg fetch
 * returns -1 on error, 1 on success, 0 if already inited (call db_close_msgfetch() first)
 */
int db_init_msgfetch(u64_t uid)
{
  msgbuf = (char*)my_malloc(MSGBUF_WINDOWSIZE);
  if (!msgbuf)
    return -1;

  if (_msg_fetch_inited)
    return 0;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblks WHERE "
	   "message_idnr = %llu ORDER BY messageblk_idnr", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): could not get message\n");
      return (-1);
    }

  _msg_result = res; /* save query result */

  nblocks = PQntuples(_msg_result);
  if (nblocks == 0)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): message has no blocks\n");
      PQclear(_msg_result);
      return -1;                     /* msg should have 1 block at least */
    }
  
  _msg_fetch_inited = 1;
  msgidx = 0;

  /* start at row (tuple) 0 */
  _msgrow_idx = 0;

  rowlength = PQgetlength(_msg_result, _msgrow_idx, 0);
  strncpy(msgbuf, PQgetvalue(_msg_result, _msgrow_idx, 0), MSGBUF_WINDOWSIZE-1);

  zeropos.block = 0;
  zeropos.pos = 0;

  if (rowlength >= MSGBUF_WINDOWSIZE-1)
    {
      buflen = MSGBUF_WINDOWSIZE-1;
      rowpos = MSGBUF_WINDOWSIZE;            /* remember store pos */
      msgbuf[buflen] = '\0';                 /* terminate buff */
      return 1;                              /* msgbuf full */
    }

  buflen = rowlength;   /* NOTE \0 has been copied from the result set */
  rowpos = rowlength;   /* no more to read from this row */

  _msgrow_idx++;
  if (_msgrow_idx >= PQntuples(_msg_result))
    {
      rowlength = rowpos = 0;
      return 1;
    }

  rowlength = PQgetlength(_msg_result, _msgrow_idx, 0);
  rowpos = 0;
  strncpy(&msgbuf[buflen], PQgetvalue(_msg_result, _msgrow_idx, 0), 
	  MSGBUF_WINDOWSIZE - buflen - 1);

  if (rowlength <= MSGBUF_WINDOWSIZE - buflen - 1)
    {
      /* 2nd block fits entirely */
      rowpos = rowlength;
      buflen += rowlength;
    }
  else
    {
      rowpos = MSGBUF_WINDOWSIZE - (buflen+1);
      buflen = MSGBUF_WINDOWSIZE-1;
    }

  msgbuf[buflen] = '\0';           /* add NULL */
  return 1;
}


/*
 * db_update_msgbuf()
 *
 * update msgbuf:
 * if minlen < 0, update is forced else update only if there are less than 
 * minlen chars left in buf
 *
 * returns 1 on succes, -1 on error, 0 if no more chars in rows
 */
int db_update_msgbuf(int minlen)
{
  if (_msgrow_idx >= PQntuples(_msg_result))
    return 0; /* no more */

  if (msgidx > buflen)
    return -1;             /* error, msgidx should be within buf */

  if (minlen > 0 && (buflen-msgidx) > minlen)
    return 1;                                 /* ok, need no update */
      
  if (msgidx == 0)
    return 1;             /* update no use, buffer would not change */

  trace(TRACE_DEBUG,"update msgbuf updating %llu %llu %llu %llu\n",MSGBUF_WINDOWSIZE,
	buflen,rowlength,rowpos);

  /* move buf to make msgidx 0 */
  memmove(msgbuf, &msgbuf[msgidx], (buflen-msgidx));
  if (msgidx > ((buflen+1) - rowpos))
    {
      zeropos.block++;
      zeropos.pos = (msgidx - ((buflen) - rowpos));
    }
  else
    zeropos.pos += msgidx;

  buflen -= msgidx;
  msgidx = 0;

  if ((rowlength-rowpos) >= (MSGBUF_WINDOWSIZE - buflen))
    {
      trace(TRACE_DEBUG,"update msgbuf non-entire fit\n");

      /* rest of row does not fit entirely in buf */
      strncpy(&msgbuf[buflen], &( (PQgetvalue(_msg_result, _msgrow_idx, 0))[rowpos] ),
	      MSGBUF_WINDOWSIZE - buflen);
      rowpos += (MSGBUF_WINDOWSIZE - buflen - 1);

      buflen = MSGBUF_WINDOWSIZE-1;
      msgbuf[buflen] = '\0';

      return 1;
    }

  trace(TRACE_DEBUG,"update msgbuf: entire fit\n");

  strncpy(&msgbuf[buflen], &( (PQgetvalue(_msg_result, _msgrow_idx, 0))[rowpos] ), 
	  (rowlength-rowpos));
  buflen += (rowlength-rowpos);
  msgbuf[buflen] = '\0';
  rowpos = rowlength;
  
  /* try to fetch a new row */
  _msgrow_idx++;
  if (_msgrow_idx >= PQntuples(_msg_result))
    {
      trace(TRACE_DEBUG,"update msgbuf succes NOMORE\n");
      return 0;
    }

  rowlength = PQgetlength(_msg_result, _msgrow_idx, 0);
  rowpos = 0;

  trace(TRACE_DEBUG,"update msgbuf, got new block, trying to place data\n");

  strncpy(&msgbuf[buflen], PQgetvalue(_msg_result, _msgrow_idx, 0), 
	  MSGBUF_WINDOWSIZE - buflen - 1);

  if (rowlength <= MSGBUF_WINDOWSIZE - buflen - 1)
    {
      /* 2nd block fits entirely */
      trace(TRACE_DEBUG,"update msgbuf: new block fits entirely\n");

      rowpos = rowlength;
      buflen += rowlength;
    }
  else
    {
      rowpos = MSGBUF_WINDOWSIZE - (buflen+1);
      buflen = MSGBUF_WINDOWSIZE-1;
    }

  msgbuf[buflen] = '\0' ;          /* add NULL */

  trace(TRACE_DEBUG,"update msgbuf succes\n");
  return 1;
}


/*
 * db_close_msgfetch()
 *
 * finishes a msg fetch
 */
void db_close_msgfetch()
{
  if (!_msg_fetch_inited)
    return; /* nothing to be done */

  my_free(msgbuf);
  msgbuf = NULL;

  nblocks = 0;

  PQclear(_msg_result);
  _msg_fetch_inited = 0;
}

void db_give_msgpos(db_pos_t *pos)
{
/*  trace(TRACE_DEBUG, "db_give_msgpos(): msgidx %llu, buflen %llu, rowpos %llu\n",
	msgidx,buflen,rowpos);
  trace(TRACE_DEBUG, "db_give_msgpos(): (buflen)-rowpos %llu\n",(buflen)-rowpos);
  */

  if (msgidx >= ((buflen)-rowpos))
    {
      pos->block = zeropos.block+1;
      pos->pos   = msgidx - ((buflen)-rowpos);
    }
  else
    {
      pos->block = zeropos.block;
      pos->pos = zeropos.pos + msgidx;
    }
}


/*
 * db_give_range_size()
 * 
 * determines the number of bytes between 2 db_pos_t's
 *
 * ONLY VALID WHEN _MSG_RESULT CONTAINS A VALID RESULT SET!!
 */
u64_t db_give_range_size(db_pos_t *start, db_pos_t *end)
{
  int i;
  u64_t size;

  if (start->block > end->block)
    return 0; /* bad range */

  if (start->block >= nblocks || end->block >= nblocks)
    return 0; /* bad range */

  if (start->block == end->block)
    return (start->pos > end->pos) ? 0 : (end->pos - start->pos+1);

  if (start->pos > PQgetlength(_msg_result, start->block, 0) || 
      end->pos > PQgetlength(_msg_result, end->block, 0))
    return 0; /* bad range */

  size =  PQgetlength(_msg_result, start->block, 0) - start->pos;

  for (i = start->block+1; i<end->block; i++)
    size += PQgetlength(_msg_result, i, 0);

  size += end->pos;
  size++;

  return size;
}


/*
 * db_dump_range()
 *
 * dumps a range specified by start,end for the msg with ID msguid
 *
 * returns -1 on error or the number of output bytes otherwise
 */
long db_dump_range(MEM *outmem, db_pos_t start, db_pos_t end, u64_t msguid)
{
  
  u64_t i,startpos,endpos,j,bufcnt;
  u64_t outcnt;
  u64_t distance;
  char buf[DUMP_BUF_SIZE];
  char *field;

  trace(TRACE_DEBUG,"Dumping range: (%llu,%llu) - (%llu,%llu)\n",
	start.block, start.pos, end.block, end.pos);

  if (start.block > end.block)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      return -1;
    }

  if (start.block == end.block && start.pos > end.pos)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      return -1;
    }

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblks WHERE message_idnr = %llu"
	   " ORDER BY messageblk_idnr", 
	   msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_dump_range(): could not get message\n");
      return (-1);
    }

  if (start.block >= PQntuples(res))
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      PQclear(res);
      return -1;
    }

  outcnt = 0;

  /* just one block? */
  if (start.block == end.block)
    {
      /* dump everything */
      bufcnt = 0;
      field = PQgetvalue(res, start.block, 0);

      for (i=start.pos; i<=end.pos; i++)
	{
	  if (bufcnt >= DUMP_BUF_SIZE-1)
	    {
	      outcnt += mwrite(buf, bufcnt, outmem);
	      bufcnt = 0;
	    }

	  if (field[i] == '\n')
	    {
	      buf[bufcnt++] = '\r';
	      buf[bufcnt++] = '\n';
	    }
	  else
	    buf[bufcnt++] = field[i];
	}
      
      outcnt += mwrite(buf, bufcnt, outmem);
      bufcnt = 0;

      PQclear(res);
      return outcnt;
    }


  /* 
   * multiple block range specified
   */
  
  for (i=start.block, outcnt=0; i<=end.block; i++)
    {
      if (i >= PQntuples(res))
	{
	  trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
	  PQclear(res);
	  return -1;
	}

      startpos = (i == start.block) ? start.pos : 0;
      endpos   = (i == end.block) ? end.pos+1 : PQgetlength(res, i, 0);

      distance = endpos - startpos;

      /* output */
      bufcnt = 0;
      field = PQgetvalue(res, i, 0);

      for (j=0; j<distance; j++)
	{
	  if (bufcnt >= DUMP_BUF_SIZE-1)
	    {
	      outcnt += mwrite(buf, bufcnt, outmem);
	      bufcnt = 0;
	    }

	  if (field[startpos+j] == '\n')
	    {
	      buf[bufcnt++] = '\r';
	      buf[bufcnt++] = '\n';
	    }
	  else if (field[startpos+j])
	    buf[bufcnt++] = field[startpos+j];
	}
      outcnt += mwrite(buf, bufcnt, outmem);
      bufcnt = 0;
    }

  PQclear(res);

  return outcnt;
}

