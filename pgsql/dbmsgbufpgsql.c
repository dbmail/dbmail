/* 
 * dbmsgbufpgsql.c
 *
 * function implementations for the msgbuf system
 * using a pgsql database
 */

#include "../dbmsgbuf.h"
#include "../db.h"
#include "/usr/local/pgsql/include/libpq-fe.h"
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
MYSQL_ROW _msgrow;
int _msg_fetch_inited = 0;

/*
 * CONDITIONS FOR MSGBUF
 *
 * rowlength         length of current row
 * rowpos            current pos in row (_msgrow[0][rowpos-1] is last read char)
 * msgidx            index within msgbuf, 0 <= msgidx < buflen
 * buflen            current buffer length: msgbuf[buflen] == '\0'
 * zeropos           absolute position (block/offset) of msgbuf[0]
 */

char *msgbuf;
u64_t rowlength = 0,msgidx=0,buflen=0,rowpos=0;
db_pos_t zeropos;
unsigned nblocks = 0;
u64_t *blklengths = NULL;


/*
 * db_init_msgfetch()
 *  
 * initializes a msg fetch
 * returns -1 on error, 1 on success, 0 if already inited (call db_close_msgfetch() first)
 */
int db_init_msgfetch(u64_t uid)
{
  int i;
  
  msgbuf = (char*)my_malloc(MSGBUF_WINDOWSIZE);
  if (!msgbuf)
    return -1;

  if (_msg_fetch_inited)
    return 0;

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE "
	   "messageidnr = %llu ORDER BY messageblknr", uid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): could not get message\n");
      return (-1);
    }

  if ((_msg_result = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_init_msgfetch(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      return (-1);
    }

  /* first determine block lengths */
  nblocks = mysql_num_rows(_msg_result);
  if (nblocks == 0)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): message has no blocks\n");
      mysql_free_result(_msg_result);
      return -1;                     /* msg should have 1 block at least */
    }
  
  if (!(blklengths = (u64_t*)my_malloc(nblocks * sizeof(u64_t))))
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): out of memory\n");
      mysql_free_result(_msg_result);
      return (-1);
    }
     
  for (i=0; i<nblocks; i++)
    {
      _msgrow = mysql_fetch_row(_msg_result);
      blklengths[i] = (mysql_fetch_lengths(_msg_result))[0];
    }

  /* re-execute query */
  mysql_free_result(_msg_result);
  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_init_msgfetch(): could not get message\n");
      my_free(blklengths);
      blklengths = NULL;
      return (-1);
    }

  if ((_msg_result = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_init_msgfetch(): mysql_store_result failed: %s\n",
	    mysql_error(&conn));
      my_free(blklengths);
      blklengths = NULL;
      return (-1);
    }

  _msg_fetch_inited = 1;
  msgidx = 0;

  /* save rows */
  _msgrow = mysql_fetch_row(_msg_result);

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  strncpy(msgbuf, _msgrow[0], MSGBUF_WINDOWSIZE-1);
  zeropos.block = 0;
  zeropos.pos = 0;

  if (rowlength >= MSGBUF_WINDOWSIZE-1)
    {
      buflen = MSGBUF_WINDOWSIZE-1;
      rowpos = MSGBUF_WINDOWSIZE;            /* remember store pos */
      msgbuf[buflen] = '\0';                 /* terminate buff */
      return 1;                              /* msgbuf full */
    }

  buflen = rowlength;   /* NOTE \0 has been copied from _msgrow) */
  rowpos = rowlength;   /* no more to read from this row */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      rowlength = rowpos = 0;
      return 1;
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  rowpos = 0;
  strncpy(&msgbuf[buflen], _msgrow[0], MSGBUF_WINDOWSIZE - buflen - 1);

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
  if (!_msgrow)
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
      strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], MSGBUF_WINDOWSIZE - buflen);
      rowpos += (MSGBUF_WINDOWSIZE - buflen - 1);

      buflen = MSGBUF_WINDOWSIZE-1;
      msgbuf[buflen] = '\0';

      return 1;
    }

  trace(TRACE_DEBUG,"update msgbuf: entire fit\n");

  strncpy(&msgbuf[buflen], &_msgrow[0][rowpos], (rowlength-rowpos));
  buflen += (rowlength-rowpos);
  msgbuf[buflen] = '\0';
  rowpos = rowlength;
  
  /* try to fetch a new row */
  _msgrow = mysql_fetch_row(_msg_result);
  if (!_msgrow)
    {
      trace(TRACE_DEBUG,"update msgbuf succes NOMORE\n");
      return 0;
    }

  rowlength = (mysql_fetch_lengths(_msg_result))[0];
  rowpos = 0;

  trace(TRACE_DEBUG,"update msgbuf, got new block, trying to place data\n");

  strncpy(&msgbuf[buflen], _msgrow[0], MSGBUF_WINDOWSIZE - buflen - 1);
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

  my_free(blklengths);
  blklengths = NULL;
  nblocks = 0;

  mysql_free_result(_msg_result);
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

  if (start->pos > blklengths[start->block] || end->pos > blklengths[end->block])
    return 0; /* bad range */

  size = blklengths[start->block] - start->pos;

  for (i = start->block+1; i<end->block; i++)
    size += blklengths[i];

  size += end->pos;
  size++;

  return size;
}


/*
 * db_msgdump()
 *
 * dumps a message to stderr
 * returns the size (in bytes) that the message occupies in memory
 */
int db_msgdump(mime_message_t *msg, u64_t msguid, int level)
{
  struct element *curr;
  struct mime_record *mr;
  char *spaces;
  int size = sizeof(mime_message_t);

  if (level < 0)
    return 0;

  if (!msg)
    {
      trace(TRACE_DEBUG,"db_msgdump: got null\n");
      return 0;
    }

  spaces = (char*)my_malloc(3*level + 1);
  if (!spaces)
    return 0;

  memset(spaces, ' ', 3*level);
  spaces[3*level] = 0;


  trace(TRACE_DEBUG,"%sMIME-header: \n",spaces);
  curr = list_getstart(&msg->mimeheader);
  if (!curr)
    trace(TRACE_DEBUG,"%s%snull\n",spaces,spaces);
  else
    {
      while (curr)
	{
	  mr = (struct mime_record *)curr->data;
	  trace(TRACE_DEBUG,"%s%s[%s] : [%s]\n",spaces,spaces,mr->field, mr->value);
	  curr = curr->nextnode;
	  size += sizeof(struct mime_record);
	}
    }
  trace(TRACE_DEBUG,"%s*** MIME-header end\n",spaces);
     
  trace(TRACE_DEBUG,"%sRFC822-header: \n",spaces);
  curr = list_getstart(&msg->rfcheader);
  if (!curr)
    trace(TRACE_DEBUG,"%s%snull\n",spaces,spaces);
  else
    {
      while (curr)
	{
	  mr = (struct mime_record *)curr->data;
	  trace(TRACE_DEBUG,"%s%s[%s] : [%s]\n",spaces,spaces,mr->field, mr->value);
	  curr = curr->nextnode;
	  size += sizeof(struct mime_record);
	}
    }
  trace(TRACE_DEBUG,"%s*** RFC822-header end\n",spaces);

  trace(TRACE_DEBUG,"%s*** Body range:\n",spaces);
  trace(TRACE_DEBUG,"%s%s(%llu, %llu) - (%llu, %llu), size: %llu, newlines: %llu\n",
	spaces,spaces,
	msg->bodystart.block, msg->bodystart.pos,
	msg->bodyend.block, msg->bodyend.pos,
	msg->bodysize, msg->bodylines);
	

/*  trace(TRACE_DEBUG,"body: \n");
  db_dump_range(msg->bodystart, msg->bodyend, msguid);
  trace(TRACE_DEBUG,"*** body end\n");
*/
  trace(TRACE_DEBUG,"%sChildren of this msg:\n",spaces);
  
  curr = list_getstart(&msg->children);
  while (curr)
    {
      size += db_msgdump((mime_message_t*)curr->data,msguid,level+1);
      curr = curr->nextnode;
    }
  trace(TRACE_DEBUG,"%s*** child list end\n",spaces);

  my_free(spaces);
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

  snprintf(query, DEF_QUERYSIZE, "SELECT messageblk FROM messageblk WHERE messageidnr = %llu"
	   " ORDER BY messageblknr", 
	   msguid);

  if (db_query(query) == -1)
    {
      trace(TRACE_ERROR, "db_dump_range(): could not get message\n");
      return (-1);
    }

  if ((res = mysql_store_result(&conn)) == NULL)
    {
      trace(TRACE_ERROR,"db_dump_range(): mysql_store_result failed: %s\n",mysql_error(&conn));
      return (-1);
    }

  for (row = mysql_fetch_row(res), i=0; row && i < start.block; i++, row = mysql_fetch_row(res)) ;
      
  if (!row)
    {
      trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
      mysql_free_result(res);
      return -1;
    }

  outcnt = 0;

  /* just one block? */
  if (start.block == end.block)
    {
      /* dump everything */
      bufcnt = 0;
      for (i=start.pos; i<=end.pos; i++)
	{
	  if (bufcnt >= DUMP_BUF_SIZE-1)
	    {
	      outcnt += mwrite(buf, bufcnt, outmem);
	      bufcnt = 0;
	    }

	  if (row[0][i] == '\n')
	    {
	      buf[bufcnt++] = '\r';
	      buf[bufcnt++] = '\n';
	    }
	  else
	    buf[bufcnt++] = row[0][i];
	}
      
      outcnt += mwrite(buf, bufcnt, outmem);
      bufcnt = 0;

      mysql_free_result(res);
      return outcnt;
    }


  /* 
   * multiple block range specified
   */
  
  for (i=start.block, outcnt=0; i<=end.block; i++)
    {
      if (!row)
	{
	  trace(TRACE_ERROR,"db_dump_range(): bad range specified\n");
	  mysql_free_result(res);
	  return -1;
	}

      startpos = (i == start.block) ? start.pos : 0;
      endpos   = (i == end.block) ? end.pos+1 : (mysql_fetch_lengths(res))[0];

      distance = endpos - startpos;

      /* output */
      bufcnt = 0;
      for (j=0; j<distance; j++)
	{
	  if (bufcnt >= DUMP_BUF_SIZE-1)
	    {
	      outcnt += mwrite(buf, bufcnt, outmem);
	      bufcnt = 0;
	    }

	  if (row[0][startpos+j] == '\n')
	    {
	      buf[bufcnt++] = '\r';
	      buf[bufcnt++] = '\n';
	    }
	  else if (row[0][startpos+j])
	    buf[bufcnt++] = row[0][startpos+j];
	}
      outcnt += mwrite(buf, bufcnt, outmem);
      bufcnt = 0;

      row = mysql_fetch_row(res); /* fetch next row */
    }

  mysql_free_result(res);

  return outcnt;
}

