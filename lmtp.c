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
#include "dsn.h"
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
struct list rcpt;
char *envelopefrom = NULL;

/* allowed lmtp commands */
const char * const commands [] =
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
	__attribute__((format(printf, 3, 4)));
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


int lmtp(void *stream, void *instream, char *buffer, char *client_ip UNUSED, PopSession_t *session)
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
 
          session->state = LHLO;
          return 1;
        }
      case LMTP_HELP :
        {
          int helpcmd;
 
          if (value == NULL)
            helpcmd = LMTP_END;
          else
            for (helpcmd = LMTP_STRT; helpcmd < LMTP_END; helpcmd++)
              if (strcasecmp(value, commands[helpcmd]) == 0) break;
 
          trace(TRACE_DEBUG,"lmtp(): LMTP_HELP requested for commandtype %d", helpcmd);

          if( (helpcmd==LMTP_LHLO) || (helpcmd==LMTP_DATA) ||
              (helpcmd==LMTP_RSET) || (helpcmd==LMTP_QUIT) ||
              (helpcmd==LMTP_NOOP) || (helpcmd==LMTP_HELP) )
            {
              fprintf((FILE *)stream, "%s", LMTP_HELP_TEXT[helpcmd]);
            }
          else
            {
              fprintf((FILE *)stream, "%s", LMTP_HELP_TEXT[LMTP_END]);
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
              size_t tmplen = 0, tmppos = 0;
              char *tmpaddr = NULL, *tmpbody = NULL;

	      find_bounded(value, '<', '>', &tmpaddr, &tmplen, &tmppos);

              /* Second look for a BODY keyword.
               * See if it has an argument, and if we
               * support that feature. Don't give an OK
               * if we can't handle it yet, like 8BIT!
               * */
           
              /* Find the '=' following the address
               * then advance one character past it
               * (but only if there's more string!)
               * */
              tmpbody = strstr(value+tmppos, "=");
              if (tmpbody != NULL)
                  if (strlen(tmpbody))
                      tmpbody++;
           
              /* This is all a bit nested now... */
              if (tmplen < 1)
                {
                  fprintf((FILE *)stream,"500 No address found.\r\n" );
                  goodtogo = 0;
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
                  envelopefrom = tmpaddr;
                  fprintf((FILE *)stream,"250 Sender <%s> OK\r\n", envelopefrom );
                }
	      else
                {
                  if (tmpaddr != NULL)
                      my_free(tmpaddr);
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
              size_t tmplen = 0, tmppos = 0;
              char *tmpaddr = NULL;

	      find_bounded(value, '<', '>', &tmpaddr, &tmplen, &tmppos);
           
              if (tmplen < 1)
                {
                  fprintf((FILE *)stream,"500 No address found.\r\n" );
                }
              else
                {
                  /* Note that this is not a pointer, but really is on the stack!
                   * Because list_nodeadd() memcpy's the structure, we don't need
                   * it to live any longer than the duration of this stack frame. */
                  deliver_to_user_t dsnuser;

                  dsnuser_init(&dsnuser);

                  /* find_bounded() allocated tmpaddr for us, and that's ok
                   * since dsnuser_free() will free it for us later on. */
                  dsnuser.address = tmpaddr;

                  list_nodeadd(&rcpt, &dsnuser, sizeof(deliver_to_user_t));
            
		  /* FIXME: At the moment, we queue up recipients until DATA or BDAT
		   * and then reply with their statuses. This may be in violation of
		   * the pipelining requirement of LMTP... */
                  /* fprintf((FILE *)stream,"250 Recipient <%s> OK\r\n", tmprcpt );
		   * */
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
              int has_recipients = 0;
              struct element *element;
           
              /* The replies MUST be in the order received */
              rcpt.start = list_reverse(rcpt.start);

              /* Resolve the addresses into deliverable / non-deliverable form. */
              if (dsnuser_resolve_list(&rcpt) == -1)
                {
                  trace(TRACE_ERROR, "main(): dsnuser_resolve_list failed");
                  fprintf((FILE *)stream, "554 No valid recipients\r\n" );
                  return 1;
                }
           
              for(element = list_getstart(&rcpt);
                  element != NULL; element = element->nextnode)
                {
                  deliver_to_user_t *dsnuser = (deliver_to_user_t *)element->data;

                  /* Class 2 means the address was deliverable in some way. */
                  switch (dsnuser->dsn.class)
                    {
                      case DSN_CLASS_OK:
                        fprintf((FILE *)stream, "250 Recipient <%s> OK\r\n",
                                dsnuser->address);
                        has_recipients = 1;
                        break;
                      default:
                        fprintf((FILE *)stream, "550 Recipient <%s> FAIL\r\n",
                                dsnuser->address);
                        break;
                    }
                }
           
              /* Now we have a list of recipients! */
              /* Let the client know if they should continue... */
           
              if (has_recipients && envelopefrom != NULL)
                {
                  trace(TRACE_DEBUG,"main(): requesting sender to begin message.");
                  fprintf((FILE *)stream, "354 Start mail input; end with <CRLF>.<CRLF>\r\n" );
                }
              else
                {
                  if (!has_recipients)
                    {
                      trace(TRACE_DEBUG,"main(): no valid recipients found, cancel message.");
                      fprintf((FILE *)stream, "554 No valid recipients.\r\n" );
                    }
                  if (!envelopefrom)
                    {
                      trace(TRACE_DEBUG,"main(): envelopefrom is empty, cancel message.");
                      fprintf((FILE *)stream, "554 No valid sender.\r\n" );
                    }
		  return 1;
                }

	    /* Anonymous Block */
	      {
	        char *header = NULL;
                u64_t headersize=0, headerrfcsize=0;
                u64_t dummyidx=0,dummysize=0;
                struct list fromlist, headerfields;
                struct element *element;

		list_init(&mimelist);
		list_init(&fromlist);
		list_init(&headerfields);

                /* if (envelopefrom != NULL) */
                /* We know this to be true from the 354 code, above. */
                list_nodeadd(&fromlist, envelopefrom, strlen(envelopefrom));

                if (!read_header((FILE *)instream, &headerrfcsize, &headersize, &header))
		  {
                    trace(TRACE_ERROR,"main(): fatal error from read_header()");
                    fprintf((FILE *)stream, "500 Error reading header.\r\n" );
		    return 1;
		  }

                if (header != NULL)
		  {
                    trace(TRACE_ERROR,"main(): size of read_header() header is [%llu]", headersize);
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
                    fprintf((FILE *)stream, "500 Error reading header.\r\n" );
                    return 1;
                  }

	        if (insert_messages((FILE *)instream,
                                    header, headersize, headerrfcsize,
                                    &headerfields, &rcpt, &fromlist) == -1)
                  {
                    fprintf((FILE *)stream, "503 Message not received\r\n");
                  }
                else
                  {
                    fprintf((FILE *)stream, "250 Message received OK\r\n" );
           
                    for (element = list_getstart(&rcpt);
                            element != NULL; element = element->nextnode)
                      {
                        deliver_to_user_t *dsnuser = (deliver_to_user_t *)element->data;

                        switch (dsnuser->dsn.class)
                          {
                            case DSN_CLASS_OK:
                              fprintf((FILE *)stream, "250 Recipient <%s> OK\r\n",
                                      dsnuser->address);
                              break;
                            case DSN_CLASS_TEMP:
                              fprintf((FILE *)stream, "450 Recipient <%s> TEMP FAIL\r\n",
                                      dsnuser->address);
                              break;
                            case DSN_CLASS_FAIL:
                            default:
                              fprintf((FILE *)stream, "550 Recipient <%s> PERM FAIL\r\n",
                                      dsnuser->address);
                              break;
                          }
                      }
                  }
                if (header != NULL)
                    my_free(header);
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

