/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * Header.c implements functions to read an email header
 * and parse out certain goodies, such as deliver-to
 * fields and common fields for the fast header cache
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "list.h"
#include "auth.h"
#include "mime.h"
#include "header.h"
#include "db.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

extern struct list mimelist;  
extern struct list users;  
extern struct list smtpItems;  

#define HEADER_BLOCK_SIZE 1024

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

/* Reads from the specified pipe until either a lone carriage
 * return or lone period stand on a line by themselves. The
 * number of non-\r\n newlines is recorded along the way.
 * The variable "header" should be passed by & reference,
 * and should be defined (duh) but not malloc'ed (honest)
 * before calling.
 *
 * The caller is responsible for free'ing header, even upon error.
 *
 * Return values:
 *   1 on success
 *   0 on failure
 * */
int read_header(FILE *instream, u64_t *headerrfcsize, u64_t *headersize, char **header)
{
  char *tmpline;
  char *tmpheader;
  u64_t tmpheadersize=0, tmpheaderrfcsize=0;
  size_t usedmem=0, linemem=0; 
  size_t allocated_blocks=1;
  int myeof=0;

  memtst((tmpheader = (char *)my_malloc(HEADER_BLOCK_SIZE))==NULL);
  memtst((tmpline = (char *)my_malloc(MAX_LINE_SIZE))==NULL);

  /* Resetting */
  memset(tmpline, '\0', MAX_LINE_SIZE);
  memset(tmpheader, '\0', HEADER_BLOCK_SIZE);
  
  /* here we will start a loop to read in the message header */
  /* the header will be everything up until \n\n or an EOF of */
  /* in_stream (instream) */
	
  trace(TRACE_INFO, "read_header(): readheader start\n");

  while (!feof(instream) && !myeof)
    {
      /* fgets will read until \n occurs, and \n is *included* in tmpline */
      if (!fgets(tmpline, MAX_LINE_SIZE, instream))
          break;
      linemem = strlen(tmpline);
      tmpheadersize += linemem;
      tmpheaderrfcsize += linemem;
      /* The RFC size assumes all lines end in \r\n,
       * so if we have a newline (\n) but don't have
       * a carriage return (\r), count it in rfcsize. */
      if (linemem > 0 && tmpline[linemem-1] == '\n')
          if (linemem == 1 || (linemem > 1 && tmpline[linemem-2] != '\r'))
              tmpheaderrfcsize++;

      if (ferror(instream))
        {
          trace(TRACE_ERROR,"read_header(): error on instream: [%s]", strerror(errno));
	  my_free(tmpline);
          /* NOTA BENE: Make sure that the caller knows to free
           * the header block even if there's been an error! */
          return -1;
        }

      /* The end of the header could be \n\n, \r\n\r\n,
       * or \r\n.\r\n, in the accidental case that we
       * ate the whole SMTP message, too! */
      if (strcmp(tmpline, ".\r\n") == 0)
        {
          /* This is the end of the message! */
	  trace (TRACE_DEBUG,"read_header(): single period found");
          myeof = 1;
        }
      else if (strcmp(tmpline, "\n") == 0 || strcmp(tmpline, "\r\n") == 0)
        {
	  /* We've found the end of the header */
	  trace (TRACE_DEBUG,"read_header(): single blank line found");
          myeof = 1;
        }
      
      /* Even if we hit the end of the header, don't forget to copy the extra
       * returns. They will always be needed to separate the header from the
       * message during any future retrieval of the fully concatenated message.
       * */

      trace (TRACE_DEBUG,"read_header(): copying line into header");

      /* If this happends it's a very big header */	
      if (usedmem + linemem > (allocated_blocks*HEADER_BLOCK_SIZE))
        {
          /* Update block counter */
          allocated_blocks++;
          trace (TRACE_DEBUG,"read_header(): mem current: [%d] reallocated to [%d]",
                  usedmem, allocated_blocks*HEADER_BLOCK_SIZE);
          memtst((tmpheader = (char *)realloc(tmpheader, allocated_blocks*HEADER_BLOCK_SIZE))==NULL);
        }

      /* This *should* always happen, but better safe than overflowing! */
      if (usedmem + linemem < (allocated_blocks*HEADER_BLOCK_SIZE))
        {
          /* Copy starting at the current usage offset */
          strncpy( (tmpheader+usedmem), tmpline, linemem);
          usedmem += linemem;

          /* Resetting strlen for tmpline */
          tmpline[0] = '\0';
          linemem=0;
        }
    }
	
  trace (TRACE_DEBUG, "read_header(): readheader done");
  trace (TRACE_DEBUG, "read_header(): found header [%s] of len [%d] using mem [%d]",
         tmpheader, strlen(tmpheader), usedmem);

  my_free(tmpline);

  if (usedmem==0)
    {
      /* FIXME: Why are we hard aborting? That's ridiculous. */
      trace(TRACE_STOP, "read_header(): no valid mail header found\n");
      return 0;
    }

  /* Assign to the external variable */
  *header = tmpheader;
  *headersize = tmpheadersize;
  *headerrfcsize = tmpheaderrfcsize;

  /* The caller is responsible for freeing header/tmpheader. */

  trace (TRACE_INFO, "read_header(): function successfull\n");
  return 1;
}


