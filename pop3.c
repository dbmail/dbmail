/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * implementation for pop3 commands according to RFC 1081 */

#include "config.h"
#include "pop3.h"
#include "db.h"
#include "debug.h"
#include "dbmailtypes.h"
#include "auth.h"
#include "clientinfo.h"
#include "pop3.h"
#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

#define INCOMING_BUFFER_SIZE 512
#define APOP_STAMP_SIZE 255

/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 300

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 3

/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255

extern int pop_before_smtp;

int pop3 (void *stream, char *buffer, char *client_ip, PopSession_t *session);

/* allowed pop3 commands */
const char *commands [] =
{
	"quit", "user", "pass", "stat", "list", "retr", "dele", "noop", "last", "rset",
	"uidl","apop","auth","top"
};

const char validchars[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"_.!@#$%^&*()-+=~[]{}<>:;\\/ ";


int pop3_handle_connection (clientinfo_t *ci)
{
	/*
	   Handles connection and calls
	   pop command handler
	 */

	int done = 1; 				/* loop state */
	char *buffer = NULL;		/* connection buffer */
	char myhostname[64];
	int cnt; 						/* counter */
	time_t timestamp;

	PopSession_t session;		/* current connection session */

	/* setting Session variables */
	session.error_count = 0;

	session.username = NULL;
	session.password = NULL;

	session.apop_stamp = NULL;

	session.SessionResult = 0;


	/* getting hostname */
	gethostname (myhostname,64);
	myhostname[63] = 0; /* make sure string is terminated */

	buffer=(char *)my_malloc(INCOMING_BUFFER_SIZE*sizeof(char));

	if (!buffer)
	{
		trace (TRACE_MESSAGE,"pop3_handle_connection(): Could not allocate buffer");
		return 0;
	}

	/* create an unique timestamp + processid for APOP authentication */
	session.apop_stamp = (char *)my_malloc(APOP_STAMP_SIZE*sizeof(char));

	if (!session.apop_stamp)
	{
		trace (TRACE_MESSAGE,"pop3_handle_connection(): Could not allocate buffer for apop");
		return 0;
	}

	/* compile error string */
	timestamp = time(NULL);
	sprintf (session.apop_stamp,"<%d.%lu@%s>",getpid(),timestamp,myhostname);

	if (ci->tx)
	{
		/* sending greeting */
		fprintf (ci->tx,"+OK DBMAIL pop3 server ready to rock %s\r\n",
			  session.apop_stamp);
		fflush (ci->tx);
	}
	else
	{
		trace (TRACE_MESSAGE,"pop3_handle_connection(): TX stream is null!");
		return 0;
	}

	/* set authorization state */
	session.state = AUTHORIZATION;

	while (done > 0)
	{
		/* set the timeout counter */
		alarm (ci->timeout);

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
			done = -1;  /* check client eof  */
		else
		{
			alarm (0);                            				/* reset function handle timeout */
			done = pop3(ci->tx, buffer, ci->ip, &session);  /* handle pop3 commands */
		}
		fflush (ci->tx);
	}

	/* we've reached the state */
	session.state = UPDATE;

	/* memory cleanup */
	my_free(buffer);
	buffer = NULL;

	my_free(session.apop_stamp);
	session.apop_stamp = NULL;

	if (session.username != NULL && session.password != NULL)
	{
		switch (session.SessionResult)
		{
			case 0:
				{
					trace (TRACE_MESSAGE, "pop3_handle_connection(): user %s logging out"
							" [messages=%llu, octets=%llu]",
							session.username, session.virtual_totalmessages,
							session.virtual_totalsize);

					/* if everything went well, write down everything and do a cleanup */
					db_update_pop(&session);
					break;
				}

			case 1: trace (TRACE_ERROR, "pop3_handle_connection(): EOF from client, "
							" connection terminated"); break;

			case 2: trace (TRACE_ERROR, "pop3_handle_connection(): alert! possible flood attempt,"
							" closing connection"); break;

			case 3: trace (TRACE_ERROR, "pop3_handle_connection(): authorization layer failure"); break;
			case 4: trace (TRACE_ERROR, "pop3_handle_connection(): storage layer failure"); break;
		}
	}
	else
		trace (TRACE_ERROR, "pop3_handle_connection(): error, uncomplete session");

	if (session.username != NULL)
	{
		/* username cleanup */
		my_free(session.username);
		session.username = NULL;
	}

	if (session.password != NULL)
	{
		/* password cleanup */
		my_free(session.password);
		session.password = NULL;
	}

	/* reset timers */
	alarm (0);
	__debug_dumpallocs();

	return 0;
}


int pop3_error (PopSession_t *session, void *stream, const char *formatstring, ...)
{
	va_list argp;

	if (session->error_count>=MAX_ERRORS)
	{
		trace (TRACE_MESSAGE,"pop3_error(): too many errors (MAX_ERRORS is %d)",MAX_ERRORS);
		fprintf ((FILE *)stream, "-ERR loser, go play somewhere else\r\n");
		session->SessionResult = 2; /* possible flood */
		return -3;
	}
	else
	{
		va_start(argp, formatstring);
		vfprintf ((FILE *)stream, formatstring, argp);
		va_end(argp);
	}

	trace (TRACE_DEBUG,"pop3_error(): an invalid command was issued");
	session->error_count++;
	return 1;
}

int pop3 (void *stream, char *buffer, char *client_ip, PopSession_t *session)
{
	/* returns a 0  on a quit
	 *           -1  on a failure
	 *				 1  on a success */
	char *command, *value;
	int cmdtype, found=0;
	int indx=0;
	u64_t result;
	u64_t top_lines, top_messageid;
	struct element *tmpelement;
	char *md5_apop_he;
	char *searchptr;

	/* buffer overflow attempt */
	if (strlen(buffer)>MAX_IN_BUFFER)
	{
		trace(TRACE_DEBUG, "pop3(): buffer overflow attempt");
		return -3;
	}

	/* check for command issued */
	while (strchr(validchars, buffer[indx]))
		indx++;

	/* end buffer */
	buffer[indx]='\0';

	trace(TRACE_DEBUG,"pop3(): incoming buffer: [%s]",buffer);

	command=buffer;

	value=strstr (command," "); /* look for the separator */

	if (value!=NULL)
	{
		*value = '\0'; /* set a \0 on the command end */
		value++;       /* skip space */

		if (strlen(value) == 0)
			value=NULL;  /* no value specified */
		else
		{
			trace (TRACE_DEBUG,"pop3(): command issued :cmd [%s], value [%s]\n",command, value);
		}
	}

	for (cmdtype = POP3_STRT; cmdtype < POP3_END; cmdtype ++)
		if (strcasecmp(command, commands[cmdtype]) == 0) break;

	trace (TRACE_DEBUG,"pop3(): command looked up as commandtype %d", cmdtype);

	/* commands that are allowed to have no arguments */
	if ((value==NULL) && (cmdtype!=POP3_QUIT) && (cmdtype!=POP3_LIST) &&
			(cmdtype!=POP3_STAT) && (cmdtype!=POP3_RSET) && (cmdtype!=POP3_NOOP) &&
			(cmdtype!=POP3_LAST) && (cmdtype!=POP3_UIDL) && (cmdtype!=POP3_AUTH))
	{
		return pop3_error(session, stream,"-ERR your command does not compute\r\n");
	}

	switch (cmdtype)
	{
		case POP3_QUIT :
			{
				fprintf ((FILE *)stream, "+OK see ya later\r\n");
				return 0;
			}
		case POP3_USER :
			{
				if (session->state != AUTHORIZATION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				if (session->username != NULL)
				{
					/* reset username */
					my_free(session->username);
					session->username = NULL;
				}

				if (session->username == NULL)
				{
					/* create memspace for username */
					memtst((session->username = (char *)my_malloc(strlen(value)+1))==NULL);
					strncpy (session->username, value, strlen(value)+1);
				}

				fprintf ((FILE *)stream, "+OK Password required for %s\r\n",
						session->username);
				return 1;
			}

		case POP3_PASS :
			{
				if (session->state != AUTHORIZATION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				if (session->password != NULL)
				{
					my_free(session->password);
					session->password = NULL;
				}

				if (session->password == NULL)
				{
					/* create memspace for password */
					memtst((session->password = (char *)my_malloc(strlen(value)+1))==NULL);
					strncpy (session->password, value, strlen(value)+1);
				}

				/* check in authorization layer if these credentials are correct */
				result = auth_validate (session->username, session->password);

				switch (result)
				{
					case -1: session->SessionResult = 3; return -1;
					case 0:
							 {
								 trace (TRACE_ERROR,"pop3(): user [%s] tried to "
										 "login with wrong password",
										 session->username);

								 /* clear username, must be re-entered according to RFC */
								 my_free (session->username);
								 session->username = NULL;

								 /* also, if the password is set, clear it */
								 if (session->password != NULL)
								 {
									 my_free (session->password);
									 session->password = NULL;
								 }
								 return pop3_error (session, stream,"-ERR username/password incorrect\r\n");
							 }
					default:
							 {
								 /* user logged in OK */
								 session->state = TRANSACTION;

								 /* now we're going to build up a session for this user */
								 trace(TRACE_DEBUG,"pop3(): validation ok, creating session");

								 /* if pop_before_smtp is active, log this ip */
								 if (pop_before_smtp)
									 db_log_ip(client_ip);

								 result=db_createsession (result, session);
								 if (result == 1)
								 {
									 fprintf ((FILE *)stream, "+OK %s has %llu messages (%llu octets)\r\n",
											 session->username, session->virtual_totalmessages,
											 session->virtual_totalsize);
									 trace(TRACE_MESSAGE,"pop3(): user %s logged in [messages=%llu, octets=%llu]",
											 session->username, session->virtual_totalmessages,
											 session->virtual_totalsize);
									 
#ifdef PROC_TITLES
									 /* sets the program ARGV's with username */
									 set_proc_title("USER %s [%s]", session->username, client_ip);
#endif
									 
								 }
								 else
									 session->SessionResult = 4; /* something went wrong on DB layer */
								 return result;
							 }
				}
				return 1;
			}

		case POP3_LIST :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream, "-ERR wrong command mode, sir\r\n");

				tmpelement = list_getstart (&session->messagelst);
				if (value != NULL)
				{
					/* they're asking for a specific message */
					while (tmpelement != NULL)
					{
						if (((struct message *)tmpelement->data)->messageid==strtoull(value, NULL, 10) &&
								((struct message *)tmpelement->data)->virtual_messagestatus<2)
						{
							fprintf ((FILE *)stream,"+OK %llu %llu\r\n",((struct message *)tmpelement->data)->messageid,
									((struct message *)tmpelement->data)->msize);
							found = 1;
						}
						tmpelement=tmpelement->nextnode;
					}
					if (!found)
						return pop3_error (session, stream,"-ERR no such message\r\n");
					else
						return 1;
				}

				/* just drop the list */
				fprintf ((FILE *)stream, "+OK %llu messages (%llu octets)\r\n",
				 session->virtual_totalmessages,
				 session->virtual_totalsize);

				if (session->virtual_totalmessages>0)
				{
					/* traversing list */
					while (tmpelement!=NULL)
					{
						if (((struct message *)tmpelement->data)->virtual_messagestatus<2)
							fprintf ((FILE *)stream,"%llu %llu\r\n",((struct message *)tmpelement->data)->messageid,
									((struct message *)tmpelement->data)->msize);
						tmpelement=tmpelement->nextnode;
					}
				}
				fprintf ((FILE *)stream,".\r\n");
				return 1;
			}

		case POP3_STAT :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream, "-ERR wrong command mode, sir\r\n");

				fprintf ((FILE *)stream, "+OK %llu %llu\r\n",
				 session->virtual_totalmessages,
				 session->virtual_totalsize);
				
				return 1;
			}

		case POP3_RETR :
			{
				trace(TRACE_DEBUG,"pop3():RETR command, retrieving message");

				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				tmpelement=list_getstart(&(session->messagelst));
				
				/* selecting a message */
				trace(TRACE_DEBUG,"pop3(): RETR command, selecting message");
				while (tmpelement!=NULL)
				{
					if (((struct message *)tmpelement->data)->messageid==strtoull(value, NULL, 10) &&
							((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
					{
						((struct message *)tmpelement->data)->virtual_messagestatus=1;
						fprintf ((FILE *)stream,"+OK %llu octets\r\n",((struct message *)tmpelement->data)->msize);
						return db_send_message_lines ((void *)stream, ((struct message *)tmpelement->data)->realmessageid,-2, 0);
					}
					tmpelement = tmpelement->nextnode;
				}
				return pop3_error (session, stream,"-ERR no such message\r\n");
			}

		case POP3_DELE :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				tmpelement=list_getstart(&session->messagelst);
				
				/* selecting a message */
				while (tmpelement!=NULL)
				{
					if (((struct message *)tmpelement->data)->messageid==strtoull(value, NULL, 10) &&
							((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
					{
						((struct message *)tmpelement->data)->virtual_messagestatus=2;
						/* decrease our virtual list fields */
						session->virtual_totalsize-=((struct message *)tmpelement->data)->msize;
						session->virtual_totalmessages-=1;
						
						fprintf((FILE *)stream,"+OK message %llu deleted\r\n",
								((struct message *)tmpelement->data)->messageid);
						return 1;
					}
					tmpelement = tmpelement->nextnode;
				}
				return pop3_error (session, stream,"-ERR [%s] no such message\r\n",value);
			}

		case POP3_RSET :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				tmpelement=list_getstart(&session->messagelst);

				session->virtual_totalsize = session->totalsize;
				session->virtual_totalmessages = session->totalmessages;
				
				while (tmpelement!=NULL)
				{
					((struct message *)tmpelement->data)->virtual_messagestatus=((struct message *)tmpelement->data)->messagestatus;
					tmpelement=tmpelement->nextnode;
				}

				fprintf ((FILE *)stream, "+OK %llu messages (%llu octets)\r\n",
				 session->virtual_totalmessages,
				 session->virtual_totalsize);

				return 1;
			}

		case POP3_LAST :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				tmpelement = list_getstart(&session->messagelst);

				while (tmpelement!=NULL)
				{
					if (((struct message *)tmpelement->data)->virtual_messagestatus==0)
					{
						/* we need the last message that has been accessed */
						fprintf ((FILE *)stream, "+OK %llu\r\n",((struct message *)tmpelement->data)->messageid-1);
						return 1;
					}
					tmpelement = tmpelement->nextnode;
				}
				
				/* all old messages */
				fprintf ((FILE *)stream, "+OK %llu\r\n",
				 session->virtual_totalmessages);
				
				return 1;
			}

		case POP3_NOOP :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				fprintf ((FILE *)stream, "+OK\r\n");
				return 1;
			}

		case POP3_UIDL :
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				tmpelement = list_getstart(&session->messagelst);
				
				if (value!=NULL)
				{
					/* they're asking for a specific message */
					while (tmpelement!=NULL)
					{
						if (((struct message *)tmpelement->data)->messageid==strtoull(value, NULL, 10)&&
								((struct message *)tmpelement->data)->virtual_messagestatus<2)
						{
							fprintf ((FILE *)stream,"+OK %llu %s\r\n",((struct message *)tmpelement->data)->messageid,
									((struct message *)tmpelement->data)->uidl);
							found = 1;
						}
						tmpelement = tmpelement->nextnode;
					}
					if (!found)
						return pop3_error (session, stream,"-ERR no such message\r\n");
					else
						return 1;
				}
				
				/* just drop the list */
				fprintf ((FILE *)stream, "+OK Some very unique numbers for you\r\n");
				
				if (session->virtual_totalmessages > 0)
				{
					/* traversing list */
					while (tmpelement!=NULL)
					{
						if (((struct message *)tmpelement->data)->virtual_messagestatus<2)
							fprintf ((FILE *)stream,"%llu %s\r\n",((struct message *)tmpelement->data)->messageid,
									((struct message *)tmpelement->data)->uidl);

						tmpelement = tmpelement->nextnode;
					}
				}
				
				fprintf ((FILE *)stream,".\r\n");
				
				return 1;
			}

		case POP3_APOP:
			{
				if (session->state != AUTHORIZATION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				/* find out where the md5 hash starts */
				searchptr=strstr(value," ");

				if (searchptr==NULL)
					return pop3_error (session, stream,"-ERR your command does not compute\r\n");

				/* skip the space */
				searchptr = searchptr+1;

				/* value should now be the username */
				value[searchptr-value-1]='\0';

				if (strlen(searchptr)>32)
					return pop3_error(session, stream,"-ERR the thingy you issued is not a valid md5 hash\r\n");

				/* create memspace for md5 hash */
				memtst((md5_apop_he=(char *)my_malloc(strlen(searchptr)+1))==NULL);
				strncpy (md5_apop_he,searchptr,strlen(searchptr)+1);

				/* create memspace for username */ /* FIXME: we should do a length check here ! */
				memtst((session->username = (char *)my_malloc(strlen(value)+1))==NULL);
				strncpy (session->username,value,strlen(value)+1);

				/*
				 * check the encryption used for this user
				 * note that if the user does not exist it is not noted
				 * by db_getencryption()
				 */
				if (strcasecmp(auth_getencryption(auth_user_exists(session->username)), "") != 0)
				{
					/* it should be clear text */
					my_free(md5_apop_he);
					my_free(session->username);
					session->username = NULL;
					md5_apop_he = 0;
					return pop3_error(session, stream,"-ERR APOP command is not supported for this user\r\n");
				}

				trace (TRACE_DEBUG,"pop3(): APOP auth, username [%s], md5_hash [%s]",
			  session->username,
			  md5_apop_he);

				result = auth_md5_validate (session->username, md5_apop_he, session->apop_stamp);

				my_free(md5_apop_he);
				md5_apop_he = 0;

				switch (result)
				{
					case -1: session->SessionResult = 3; return -1;
					case 0:
							 trace (TRACE_ERROR,"pop3(): user [%s] tried to login with wrong password",
									 session->username);
						
							 my_free (session->username);
							 session->username = NULL;
						
							 my_free (session->password);
							 session->password = NULL;

							 return pop3_error(session, stream,"-ERR authentication attempt is invalid\r\n");

					default:
							 {
								 session->state = TRANSACTION;
								 
								 /* user seems to be valid, let's build a session */
								 trace(TRACE_DEBUG,"pop3(): validation OK, building a session for user [%s]",
					session->username);
								 
								 result=db_createsession(result, session);
								 
								 if (result == 1)
								 {
									 fprintf((FILE *)stream, "+OK %s has %llu messages (%llu octets)\r\n",
											 session->username, session->virtual_totalmessages,
						session->virtual_totalsize);
									 
									 trace(TRACE_MESSAGE,"pop3(): user %s logged in [messages=%llu, octets=%llu]",
											 session->username, session->virtual_totalmessages,
											 session->virtual_totalsize);
#ifdef PROC_TITLES
									 set_proc_title("USER %s [%s]", session->username, client_ip);
#endif
								 }
								 else
									 session->SessionResult = 4; /* storage layer error */
								 
								 return result;
							 }
				}
				return 1;
			}

		case POP3_AUTH:
			{
				if (session->state != AUTHORIZATION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");
				
				fprintf  ((FILE *)stream, "+OK List of supported mechanisms\r\n.\r\n");
				return 1;
			}

		case POP3_TOP:
			{
				if (session->state != TRANSACTION)
					return pop3_error(session, stream,"-ERR wrong command mode, sir\r\n");

				/* find out how many lines they want */
				searchptr = strstr(value," ");

				/* insufficient parameters */
				if (searchptr == NULL)
					return pop3_error (session, stream,"-ERR your command does not compute\r\n");

				/* skip the space */
				searchptr = searchptr + 1;

				/* value should now be the the message that needs to be retrieved */
				value[searchptr-value-1] = '\0';

				top_lines = strtoull(searchptr, NULL, 10);
				top_messageid = strtoull(value, NULL, 10);

				if ((top_lines<0) || (top_messageid<1))
					return pop3_error(session, stream,"-ERR wrong parameter\r\n");

				trace(TRACE_DEBUG,"pop3():TOP command (partially) retrieving message");

				tmpelement=list_getstart(&session->messagelst);
				
				/* selecting a message */
				trace(TRACE_DEBUG,"pop3(): TOP command, selecting message");
				
				while (tmpelement != NULL)
				{
					if (((struct message *)tmpelement->data)->messageid==top_messageid &&
							((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
					{
						fprintf ((FILE *)stream,"+OK %llu lines of message %llu\r\n",top_lines, top_messageid);
						return db_send_message_lines (stream, ((struct message *)tmpelement->data)->realmessageid, top_lines, 0);
					}
					tmpelement = tmpelement->nextnode;
				}
				return pop3_error (session, stream,"-ERR no such message\r\n");

				return 1;
			}

		default :
			{
				return pop3_error(session, stream,"-ERR command not understood, sir\r\n");
			}
	}
	return 1;
}
