/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * implementation for lmtp commands according to RFC 1081 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "lmtp.h"
#include "pipe.h"
#include "header.h"
#include "db.h"
#include "debug.h"
#include "dbmailtypes.h"
#include "auth.h"
#include "clientinfo.h"
#include "lmtp.h"
#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

#define INCOMING_BUFFER_SIZE 512

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

/* This one needs global score for bounce.c */
struct list mimelist;

/* These are needed across multiple calls to lmtp() */
struct list rcpt, userids, fwds;
char *envelopefrom = NULL;

/* allowed lmtp commands */
const char *commands [] =
{
  "LHLO", "QUIT", "RSET", "DATA", "MAIL",
  "VRFY", "EXPN", "HELP", "NOOP", "RCPT"
};

const char validchars[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"_.!@#$%^&*()-+=~[]{}<>:;\\/ ";

char myhostname[64];

int lmtp_handle_connection(clientinfo_t *ci)
{
  /*
     Handles connection and calls
     lmtp command handler
   */

  int done = 1;         /* loop state */
  char *buffer = NULL;    /* connection buffer */
  int cnt;            /* counter */

  PopSession_t session;   /* current connection session */

  /* setting Session variables */
  session.error_count = 0;

  session.username = NULL;
  session.password = NULL;

  session.SessionResult = 0;

  /* reset counters */
  session.totalsize = 0;
  session.virtual_totalsize = 0;
  session.totalmessages = 0;
  session.virtual_totalmessages = 0;


  /* getting hostname */
  gethostname(myhostname,64);
  myhostname[63] = 0; /* make sure string is terminated */

  buffer=(char *)my_malloc(INCOMING_BUFFER_SIZE*sizeof(char));

  if (!buffer)
    {
      trace(TRACE_MESSAGE,"lmtp_handle_connection(): Could not allocate buffer");
      return 0;
    }

  if (ci->tx)
    {
      /* sending greeting */
      fprintf(ci->tx,"220 %s DBMail LMTP service ready to rock\r\n",
          myhostname);
      fflush(ci->tx);
    }
  else
    {
      trace(TRACE_MESSAGE,"lmtp_handle_connection(): TX stream is null!");
      return 0;
    }

  while (done > 0)
    {
      /* set the timeout counter */
      alarm(ci->timeout);
 
      /* clear the buffer */
      memset(buffer, 0, INCOMING_BUFFER_SIZE);
 
      for (cnt=0; cnt < INCOMING_BUFFER_SIZE-1; cnt++)
        {
          do
            {
              clearerr(ci->rx);
              fread(&buffer[cnt], 1, 1, ci->rx);
           
              /* leave, an alarm has occured during fread */
              if (!ci->rx) return 0;
            } while (ferror(ci->rx) && errno == EINTR);

          if (buffer[cnt] == '\n' || feof(ci->rx) || ferror(ci->rx))
            {
              buffer[cnt+1] = '\0';
              break;
            }
        }

      if (feof(ci->rx) || ferror(ci->rx))
        {
          /* check client eof  */
          done = -1;
        }
      else
        {
          /* reset function handle timeout */
          alarm(0);
          /* handle lmtp commands */
          done = lmtp(ci->tx, ci->rx, buffer, ci->ip, &session);
        }
      fflush(ci->tx);
  }

  /* memory cleanup */
  my_free(buffer);
  buffer = NULL;

  /* reset timers */
  alarm(0);
  __debug_dumpallocs();

  return 0;
}


int lmtp_reset(PopSession_t *session)
  {
    /* Free the lists and reinitialize 
     * but only if they were previously
     * initialized by LMTP_LHLO... */
    if( session->state == LHLO )
      {
        list_freelist( &rcpt.start );
        list_init( &rcpt );
        list_freelist( &userids.start );
        list_init( &userids );
        list_freelist( &fwds.start );
        list_init( &fwds );
      }
 
    if( envelopefrom != NULL )
      {
        my_free( envelopefrom );
      }
    envelopefrom = NULL;
 
    session->state = LHLO;

    return 1;
  }


int lmtp_error(PopSession_t *session, void *stream, const char *formatstring, ...)
{
  va_list argp;

  if (session->error_count>=MAX_ERRORS)
    {
      trace(TRACE_MESSAGE,"lmtp_error(): too many errors (MAX_ERRORS is %d)",MAX_ERRORS);
      fprintf((FILE *)stream, "500 Too many errors, closing connection.\r\n");
      session->SessionResult = 2; /* possible flood */
      lmtp_reset(session);
      return -3;
    }
  else
    {
      va_start(argp, formatstring);
      vfprintf((FILE *)stream, formatstring, argp);
      va_end(argp);
    }

  trace(TRACE_DEBUG,"lmtp_error(): an invalid command was issued");
  session->error_count++;
  return 1;
}


int lmtp(void *stream, void *instream, char *buffer, char *client_ip, PopSession_t *session)
{
  /* returns values:
   *  0 to quit
   * -1 on failure
   *  1 on success */
  char *command, *value;
  int cmdtype;
  int indx=0;

  /* buffer overflow attempt */
  if (strlen(buffer) > MAX_IN_BUFFER)
    {
      trace(TRACE_DEBUG, "lmtp(): buffer overflow attempt");
      return -3;
    }

  /* check for command issued */
  while (strchr(validchars, buffer[indx]))
    indx++;

  /* end buffer */
  buffer[indx]='\0';

  trace(TRACE_DEBUG,"lmtp(): incoming buffer: [%s]",buffer);

  command = buffer;

  value = strstr(command," "); /* look for the separator */

  if (value!=NULL)
    {
      *value = '\0'; /* set a \0 on the command end */
      value++;       /* skip space */
 
      if (strlen(value) == 0)
        {
          value=NULL;  /* no value specified */
        }
      else
        {
          trace(TRACE_DEBUG,"lmtp(): command issued :cmd [%s], value [%s]\n",command, value);
        }
    }

  for (cmdtype = LMTP_STRT; cmdtype < LMTP_END; cmdtype ++)
    if (strcasecmp(command, commands[cmdtype]) == 0) break;

  trace(TRACE_DEBUG,"lmtp(): command looked up as commandtype %d", cmdtype);

  /* commands that are allowed to have no arguments */
  if ((value==NULL) &&
      !(
      (cmdtype==LMTP_LHLO) || (cmdtype==LMTP_DATA) ||
      (cmdtype==LMTP_RSET) || (cmdtype==LMTP_QUIT) ||
      (cmdtype==LMTP_NOOP) || (cmdtype==LMTP_HELP)
      ))
    {
      return lmtp_error(session, stream, "500 This command requires an argument.\r\n");
    }

  switch (cmdtype)
    {
      case LMTP_QUIT :
        {
          fprintf((FILE *)stream, "221 %s BYE\r\n", myhostname);
          lmtp_reset(session);
          return 0; /* return 0 to cause the connection to close */
        }
      case LMTP_NOOP :
        {
          fprintf((FILE *)stream, "250 OK\r\n");
          return 1;
        }
      case LMTP_RSET :
        {
          fprintf((FILE *)stream, "250 OK\r\n");
	  lmtp_reset(session);
          return 1;
        }
      case LMTP_LHLO :
        {
          /* Reply wth our hostname and a list of features.
           * The RFC requires a couple of SMTP extensions
           * with a MUST statement, so just hardcode them.
           * */
          fprintf((FILE *)stream,
              "250-%s\r\n"
              "250-PIPELINING\r\n"
              "250-ENHANCEDSTATUSCODES\r\n"
              /* This is a SHOULD implement:
               * "250-8BITMIME\r\n"
               * Might as well do these, too:
               * "250-CHUNKING\r\n"
               * "250-BINARYMIME\r\n"
               * */
              "250 SIZE\r\n", myhostname );
          /* Free the recipients list and reinitialize it */
          // list_freelist( &rcpt.start );
          list_init( &rcpt );
          // list_freelist( &userids.start );
          list_init( &userids );
          // list_freelist( &fwds.start );
          list_init( &fwds );
 
          session->state = LHLO;
          return 1;
        }
      case LMTP_HELP :
        {
          int helpcmd;
 
          if (value != NULL)
            for (helpcmd = LMTP_STRT; helpcmd < LMTP_END; helpcmd++)
              if (strcasecmp(value, commands[helpcmd]) == 0) break;
 
          trace(TRACE_DEBUG,"lmtp(): LMTP_HELP requested for commandtype %d", helpcmd);

          if( (helpcmd==LMTP_LHLO) || (helpcmd==LMTP_DATA) ||
              (helpcmd==LMTP_RSET) || (helpcmd==LMTP_QUIT) ||
              (helpcmd==LMTP_NOOP) || (helpcmd==LMTP_HELP) )
            {
              fprintf((FILE *)stream, LMTP_HELP_TEXT[helpcmd]);
            }
          else
            {
              fprintf((FILE *)stream, LMTP_HELP_TEXT[LMTP_STRT]);
            }
 
          return 1;
        }
      case LMTP_VRFY :
        {
          /* RFC 2821 says this SHOULD be implemented...
           * and the goal is to say if the given address
           * is a valid delivery address at this server. */
          fprintf((FILE *)stream, "502 Command not implemented\r\n" );
          return 1;
        }
      case LMTP_EXPN:
        {
          /* RFC 2821 says this SHOULD be implemented...
           * and the goal is to return the membership
           * of the specified mailing list. */
          fprintf((FILE *)stream, "502 Command not implemented\r\n" );
          return 1;
        }
      case LMTP_MAIL:
        {
          /* We need to LHLO first because the client
           * needs to know what extensions we support.
           * */
          if (session->state != LHLO)
            {
              fprintf((FILE *)stream, "550 Command out of sequence.\r\n");
            }
          else if (envelopefrom != NULL)
            {
              fprintf((FILE *)stream, "500 Sender already received. Use RSET to clear.\r\n");
            }
          else
            {
              /* First look for an email address.
               * Don't bother verifying or whatever,
               * just find something between angle brackets!
               * */
              int goodtogo=1;
              size_t tmplen=0;
              char *tmpleft=NULL, *tmpright=NULL, *tmpbody=NULL;
           
              tmpleft = value;
              tmpright = value + strlen(value);
           
              /* eew pointer math... inspired by injector.c */
           
              while (tmpleft[0] != '<' && tmpleft < tmpright )
                tmpleft++;
              while (tmpright[0] != '>' && tmpright > tmpleft )
                tmpright--;
           
              /* Step left up to skip '<' left angle bracket */
              tmpleft++;
           
              tmplen = tmpright - tmpleft;
           
              /* Second look for a BODY keyword.
               * See if it has an argument, and if we
               * support that feature. Don't give an OK
               * if we can't handle it yet, like 8BIT!
               * */
           
              /* Find the '=' following the address
               * then advance one character past it
               * (but only if there's more string!)
               * */
              tmpbody = strstr(tmpright, "=");
              if (tmpbody != NULL)
                  if (strlen(tmpbody))
                      tmpbody++;
           
              /* This is all a bit nested now... */
              if (tmplen < 1)
                {
                  fprintf((FILE *)stream,"500 No address found.\r\n" );
                }
              else if (tmpbody != NULL)
                {
                  /* See RFC 3030 for the best
                   * description of this stuff.
                   * */
                  if (strlen(tmpbody) < 4)
                    {
                      /* Caught */
                    }
                  else if (0 == strcasecmp(tmpbody, "7BIT"))
                    {
                      /* Sure fine go ahead. */
                      goodtogo = 1; // Not that it wasn't 1 already ;-)
                    }
                  /* 8BITMIME corresponds to RFC 1652,
                   * BINARYMIME corresponds to RFC 3030.
                   * */
                  else if (strlen(tmpbody) < 8)
                    {
                      /* Caught */
                    }
                  else if (0 == strcasecmp(tmpbody, "8BITMIME"))
                    {
                      /* We can't do this yet. */
                      /* session->state = BIT8;
                       * */
                      fprintf((FILE *)stream,"500 Please use 7BIT MIME only.\r\n");
                      goodtogo = 0;
                    }
                  else if (strlen(tmpbody) < 10)
                    {
                      /* Caught */
                    }
                  else if (0 == strcasecmp(tmpbody, "BINARYMIME"))
                    {
                      /* We can't do this yet. */
                      /* session->state = BDAT;
                       * */
                      fprintf((FILE *)stream,"500 Please use 7BIT MIME only.\r\n" );
                      goodtogo = 0;
                    }
                }

	      if (goodtogo)
                {
                  /* Sure fine go ahead. */
                  memtst((envelopefrom=(char *)my_malloc(tmplen+1))==NULL);
                  memset(envelopefrom,0,tmplen+1);
                  strncpy(envelopefrom,tmpleft,tmplen);
                  // envelopefrom[tmplen+1] = '\0';
                  fprintf((FILE *)stream,"250 Sender <%s> OK\r\n", envelopefrom );
                }
            }
          return 1;
        }
      case LMTP_RCPT :
        {
          /* This would be the non-piplined version...
          else if (0 < auth_check_user_ext(value, userids, fwds, -1))
          {
            fprintf((FILE *)stream, "250 OK\r\n" );
          }
          else
          {
            fprintf((FILE *)stream, "550 No such user here\r\n" );
          }
          return 1;
          */
 
          if (session->state != LHLO)
            {
              fprintf((FILE *)stream, "550 Command out of sequence.\r\n");
            }
          else
            {
              size_t tmplen;
              char *tmpleft, *tmpright, *tmprcpt;
           
              tmpleft = value;
              tmpright = value + strlen(value);
           
              /* eew pointer math... inspired by injector.c */
           
              while (tmpleft[0] != '<' && tmpleft < tmpright )
                tmpleft++;
              while (tmpright[0] != '>' && tmpright > tmpleft )
                tmpright--;
           
              /* Step left up to skip '<' left angle bracket */
              tmpleft++;
           
              tmplen = tmpright - tmpleft;
           
              if (tmplen < 1)
                {
                  fprintf((FILE *)stream,"500 No address found.\r\n" );
                }
              else
                {
                  /* Note that list_nodeadd cannot NULL terminate
                   * because it does not know what kind of data it gets! */
                  memtst((tmprcpt=(char *)my_malloc(tmplen+1))==NULL);
                  memset(tmprcpt,0,tmplen+1);
                  strncpy(tmprcpt,tmpleft,tmplen);
                  // tmprcpt[tmplen+1] = 0;
            
                  /* Just add it to the list, and process at DATA time */
                  /* Make sure to pass the terminator at +1 */
                  list_nodeadd(&rcpt, tmprcpt, tmplen+1);
            
                  /* Is there a way to know if the client wants
                   * to pipeline or not? The RFC for LMTP implies that
                   * they will pipeline, because pipelining is mandatory
                   * for LMTP servers... but... I dunno? What if they're waiting?
                   */
                  // fprintf((FILE *)stream,"250 Recipient <%s> OK\r\n", tmprcpt );

                  my_free(tmprcpt);
                }
            }
          return 1;
        }
      /* Here's where it gets really exciting! */
      case LMTP_DATA:
        {
          // if (session->state != DATA || session->state != BIT8)
          if (session->state != LHLO)
            {
              fprintf((FILE *)stream, "550 Command out of sequence\r\n" );
            }
          else if (list_totalnodes(&rcpt) < 1)
            {
              fprintf((FILE *)stream, "554 No valid recipients\r\n" );
            }
          else
            {
              struct element *tmpnode;
           
              /* The replies MUST be in the order received */
              rcpt.start = list_reverse(rcpt.start);
           
              tmpnode = list_getstart(&rcpt);
              while( tmpnode != NULL )
                {
                  if (auth_check_user_ext(tmpnode->data, &userids, &fwds, -1) > 0 )
                    {
                      fprintf((FILE *)stream, "250 Recipient <%s> OK\r\n", (char *)tmpnode->data );
                    }
                  else
                    {
                      fprintf((FILE *)stream, "550 Recipient <%s> FAIL\r\n", (char *)tmpnode->data );
                    }
                  tmpnode = tmpnode->nextnode;
                }
           
              /* Now we have a list of recipients! */
              /* Let the client know if they should continue... */
           
              if (list_totalnodes(&userids) > 0 || list_totalnodes(&fwds) > 0)
                {
                  fprintf((FILE *)stream, "354 Start mail input; end with <CRLF>.<CRLF>\r\n" );
                }
              else
                {
                  fprintf((FILE *)stream, "554 No valid recipients.\r\n" );
		  return 1;
                }

              /* If we returned due to no recipients, and the remote
	       * system starts sending a message... well... they'll
	       * get disconnected pretty quickly from max_errors.
               * */
	      {
	        char *header = NULL;
                u64_t headersize=0, newlines=0;
                u64_t dummyidx=0,dummysize=0;
                struct list fromlist, headerfields, errusers;
                struct element *tmpnode_rcpt;
                struct element *tmpnode_errs;

		list_init(&errusers);
		list_init(&mimelist);
		list_init(&fromlist);
		list_init(&headerfields);

                if (envelopefrom != NULL)
                    list_nodeadd(&fromlist, envelopefrom, strlen(envelopefrom));
                else
                  {
                    trace(TRACE_DEBUG,"main(): envelopefrom is empty so no go");
                    fprintf((FILE *)stream, "554 No valid sender.\r\n" );
		    return 1;
                  }

                if (!read_header((FILE *)instream, &newlines, &headersize, &header))
		  {
                    trace(TRACE_ERROR,"main(): fatal error from read_header()");
                    fprintf((FILE *)stream, "500 Error reading header.\r\n" );
		    return 1;
		  }

                trace(TRACE_ERROR,"main(): lines of read_header() header is [%d]", newlines);
       
                if (header != NULL)
		  {
                    trace(TRACE_ERROR,"main(): size of read_header() header is [%d]", headersize);
		  }
                else
                  {
                    trace(TRACE_ERROR,"main(): read_header() returned a null header [%s]", header);
                    fprintf((FILE *)stream, "500 Error reading header.\r\n" );
                    return 1;
                  }

                /* Parse the list and scan for field and content */
                if (mime_readheader(header, &dummyidx, &mimelist, &dummysize) < 0)
		  {
                    trace(TRACE_ERROR,"main(): fatal error from mime_readheader()");
		    return 1;
		  }

		/* FIXME: A negative return code from insert_messages() means
		 * that delivery died halfway through. There may be much more
		 * data coming from the client that we need to discard after
		 * giving back an unsuccessful return code. */
	        insert_messages((FILE *)instream, header, headersize,
				&rcpt, &errusers,
				&fromlist, 0,
				NULL, &headerfields);
                if (header != NULL)
                    my_free(header);

                /* Use the 250 code if 1 or more deliveries were successful,
                 * and report the errors individually later
                 * Use the 503 code is 0 deliveries went through.
                 *
                 * This is a weird little assumption... that if there
                 * was some problem assembling the list of errors,
                 * assume everyone failed. Basically it's all we
                 * can do since otherwise we can't know who did or
                 * did not fail anyways!
                 * */
                if (rcpt.total_nodes != errusers.total_nodes)
                  {
                    fprintf((FILE *)stream, "503 Message not received %ld FAIL\r\n", errusers.total_nodes );

                    tmpnode_rcpt = list_getstart(&rcpt);
                    while (tmpnode_rcpt != NULL )
                      {
                        fprintf((FILE *)stream, "450 Recipient <%s> FAIL\r\n", (char *)tmpnode_rcpt->data );
			tmpnode_rcpt = tmpnode_rcpt->nextnode;
                      }
                  }
                else
                  {
                    fprintf((FILE *)stream, "250 Message received OK\r\n" );
           
                    /* The replies MUST be in the order received */
                    rcpt.start = list_reverse(rcpt.start);
                   
                    /* The errors MUST be in the same order as rcpt */
                    errusers.start = list_reverse(errusers.start);
                   
                    tmpnode_rcpt = list_getstart(&rcpt);
                    tmpnode_errs = list_getstart(&errusers);
                    while (tmpnode_rcpt != NULL && tmpnode_errs != NULL)
                      {
                        /* These are evil magic numbers
                         * which must match pipe.c
                         * FIXME: define these!
                         * */
                        switch ((int)tmpnode_errs->data)
                          {
                            case 1:
                              fprintf((FILE *)stream, "250 Recipient <%s> OK\r\n", (char *)tmpnode_rcpt->data );
                              break;
                            case 0:
                              fprintf((FILE *)stream, "450 Recipient <%s> FAIL\r\n", (char *)tmpnode_rcpt->data );
                              break;
                            default:
                              fprintf((FILE *)stream, "450 Recipient <%s> FAIL\r\n", (char *)tmpnode_rcpt->data );
                              break;
                          }
                        tmpnode_rcpt = tmpnode_rcpt->nextnode;
                        tmpnode_errs = tmpnode_errs->nextnode;
                      }
                  }
               }
            }
          return 1;
        }
      default :
        {
          return lmtp_error(session, stream,"500 What are you trying to say here?\r\n");
        }
    }
  return 1;
}

