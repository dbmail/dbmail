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
 * number of newlines is recorded along the way. The variable
 * "header" should be passed by & reference, and should be
 * defined (duh) but not malloc'ed (honest) before calling.
 *
 * The caller is responsible for free'ing header, even upon error.
 *
 * Return values:
 *   1 on success
 *   0 on failure
 * */
int read_header(FILE *instream, u64_t *newlines, u64_t *headersize, char **header)
{
  char *tmpline;
  char *tmpheader;
  int usedmem=0, linemem=0; 
  int myeof=0;
  int allocated_blocks=1;

  *headersize = 0;
  *newlines = 0;
	
  memtst ((tmpheader = (char *)my_malloc(HEADER_BLOCK_SIZE))==NULL);
  memtst ((tmpline = (char *)my_malloc(MAX_LINE_SIZE))==NULL);

  /* Resetting */
  memset (tmpline, '\0', MAX_LINE_SIZE);
  memset (tmpheader, '\0', HEADER_BLOCK_SIZE);
  
  /* here we will start a loop to read in the message header */
  /* the header will be everything up until \n\n or an EOF of */
  /* in_stream (instream) */
	
  trace (TRACE_INFO, "read_header(): readheader start\n");

  while (!feof(instream) && !myeof)
    {
      /* fgets will read until \n occurs, and \n is *included* in tmpline */
	    if (!fgets(tmpline, MAX_LINE_SIZE, instream))
		    break;
      linemem = strlen(tmpline);
      (*headersize) += linemem;
      (*newlines)++;

      if (ferror(instream))
        {
          trace(TRACE_ERROR,"read_header(): error on instream: [%s]", strerror(errno));
          if (tmpline != NULL)
	      my_free(tmpline);
          /* FIXME: Make sure that the caller knows to free
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

  if (tmpline != NULL)
      my_free(tmpline);

  if (usedmem==0)
    {
      trace (TRACE_STOP, "read_header(): no valid mail header found\n");
      return 0;
    }

  /* Assign to the external variable */
  *header = tmpheader;

  /* The caller is responsible for freeing header/tmpheader. */

  trace (TRACE_INFO, "read_header(): function successfull\n");
  return 1;
}



/*
 * read_header_process()
 *
 * Reads in a mail header (from instream, reading is done until '\n\n' is encountered).
 * If field != NULL scanning is done for delivery on that particular field.
 *
 * Data is saved in hdrdata which should be capable of holding at least READ_BLOCK_SIZE characters.
 *
 * returns data cnt on success, -1 on failure
 */
int read_header_process(FILE *instream, struct list *userids, struct list *bounces, struct list *fwds, 
			const char *field, char *hdrdata, u64_t *newlines, char **bounce_path)
{
  int len = field ? strlen(field) : 0;
  char *left, *right, *curr, save, *line;
  char *frompath = 0;
  unsigned cnt = 0;
  *newlines = 0;
  *bounce_path = 0;

  while (!feof(instream) && !ferror(instream) && cnt < READ_BLOCK_SIZE)
    {
      line = &hdrdata[cnt]; /* write directly to hdrdata */
      fgets(line, READ_BLOCK_SIZE - cnt, instream);
      (*newlines)++;

      cnt += strlen(line);

      if (strcmp(line, "\n") == 0)
	break;

      if (field && strncasecmp(line, field, len) == 0 && line[len] == ':' && line[len+1] == ' ')
	{
	  /* found the field we're scanning for */
	  trace(TRACE_DEBUG, "read_header_process(): found field");
	      
	  curr = &line[len];

	  while (curr && *curr)
	    {
	      left = strchr(curr, '@');
	      if (!left)
		break;

	      right = left;

	      /* walk to the left */
	      while (left != line && left[0]!='<' && left[0]!=' ' && left[0]!='\0' && left[0]!=',')
		left--;
		  
	      left++; /* walked one back too far */
		  
	      /* walk to the right */
	      while (right[0]!='>' && right[0]!=' ' && right[0]!='\0' && right[0]!=',')
		right++;
		  
	      save = *right;
	      *right = 0; /* terminate string */
	      
	      if (add_address(left, userids, bounces, fwds) != 0)
		trace(TRACE_ERROR,"read_header_process(): could not add [%s]", left);

	      trace(TRACE_DEBUG,"read_header_process(): processed [%s]", left);
	      *right = save;
	      curr = right;
	    }
	}
      else if (field && !(*bounce_path) && strncasecmp(line, "return-path", strlen("return-path")) == 0)
	{
	  /* found return-path */
	  *bounce_path = (char*)my_malloc(strlen(line));
	  if (!(*bounce_path))
	    return -1;

	  left = strchr(line, ':');
	  if (left)
	    strcpy(*bounce_path, &left[1]);
	  else
	    {
	      my_free(bounce_path);
	      bounce_path = 0;
	    }
	}
      else if (field && !(*bounce_path) && !frompath && strncasecmp(line, "from", strlen("from")) == 0)
	{
	  /* found from field */
	  frompath = (char*)my_malloc(strlen(line));
	  if (!frompath)
	    return -1;

	  left = strchr(line, ':');
	  if (left)
	    strcpy(frompath, &left[1]);
	  else
	    {
	      my_free(frompath);
	      frompath = 0;
	    }
	}
    }
	
  if (frompath)
    {
      if (!(*bounce_path))
	*bounce_path = frompath;
      else
	my_free(frompath);
    }

  trace(TRACE_DEBUG,"read_header_process(): found bounce path [%s]", *bounce_path ? *bounce_path : "<<none>>");

  return cnt;
}


/*
 * add_address()
 *
 * takes an e-mail address and finds the correct delivery for it:
 * internal (numeric id), bounce, forward
 *
 * returns 0 on success, -1 on failure
 */
int add_address(const char *address, struct list *userids, struct list *bounces, struct list *fwds)
{
  char *domain;

  if (auth_check_user_ext(address, userids, fwds, -1) == 0)
    {
      /* not in alias table
       * check for a domain fwd first; if none present
       * then make it a bounce
       */
      
      domain = strchr(address, '@');
      if (!domain)
	{
	  /* ?? no '@' in address ? */
	  trace(TRACE_ERROR, "add_address(): got invalid address [%s]", address);
	}
      else
	{
	  if (auth_check_user_ext(domain, userids, fwds, -1) == 0)
	    {
	      /* ok no domain fwds either --> bounce */
	      if (list_nodeadd(bounces, address, strlen(address)+1) == 0)
		{
		  trace(TRACE_ERROR, "add_address(): could not add bounce [%s]", address);
		  return -1;
		}
	    }
	}
    }

  return 0;
}
      

/*
 * add_username()
 *
 * adds the (numeric) ID of the user uname to the list of ids.
 *
 * returns 0 on success, -1 on failure
 */
int add_username(const char *uname, struct list *userids)
{
  u64_t uid;

  switch(auth_user_exists(uname, &uid))
    {
    case -1:
      trace(TRACE_ERROR,"add_username(): error verifying user existence");
      return -1;
    case 0:
      trace(TRACE_INFO,"add_username(): non-existent user specified");
      return -1;
    default:
      trace(TRACE_DEBUG,"add_username(): adding user [%s] id [%llu] to list", uname, uid);
      if (list_nodeadd(userids, &uid, sizeof(uid)) == NULL)
	{
	  trace(TRACE_ERROR,"add_username(): out of memory");
	  list_freelist(&userids->start);
	  return -1;
	}
    }
  
  return 0;
}

