/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * implementation for pop3 commands accoording to RFC 1081 */

#include "config.h"
#include "pop3.h"
#include "dbmysql.h"

/* max_errors defines the maximum number of allowed failures */
#define MAX_ERRORS 10

extern int state; /* tells the current negotiation state of the server */
extern char *username, *password; /* session username and password */
extern struct session curr_session;
extern char *apop_stamp;			/* the APOP string */

extern int error_count;

/* allowed pop3 commands */
const char *commands [] = 
{
  "quit", "user", "pass", "stat", "list", "retr", "dele", "noop", "last", "rset",
  "uidl","apop","auth","top"
};

const char validchars[] = "-.@ abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


int pop3_error (void *stream, const char *formatstring, ...)
{
  va_list argp;
  va_start(argp, formatstring);
	
  if (error_count>=MAX_ERRORS)
    {
      trace (TRACE_MESSAGE,"pop3_error(): too many errors (MAX_ERRORS is %d)",MAX_ERRORS);
      fprintf ((FILE *)stream, "-ERR loser, go play somewhere else\r\n");
      return -1;
    }
  else
    vfprintf ((FILE *)stream, formatstring, argp);
  trace (TRACE_DEBUG,"pop3_error(): an invalid command was issued");
  error_count++;
  return 1;
}

int pop3 (void *stream, char *buffer)
{
  /* returns a 0  on a quit
   *           -1  on a failure 
   *				 1  on a success */
  char *command, *value;
  int cmdtype, found=0;
  int indx=0;
  unsigned long result;
  unsigned long top_lines, top_messageid;
  struct element *tmpelement;
  char *md5_apop_he;
  char *searchptr;
	
  while (strchr(validchars, buffer[indx]))
    indx++;

  buffer[indx]='\0';
	
  trace(TRACE_DEBUG,"pop3(): incoming buffer: [%s]",buffer);
	
  command=buffer;
	
  value=strstr (command," "); /* look for the separator */
	
  if (value!=NULL) /* none found */
    {
      /* clear the last \0 */
      value[indx-1]='\0';
	
      if (strlen(value)==1)
	value=NULL; /* there is only a space! */
      else 
	{
	  /* set value one further then the space */
	  value++;

			/* set a \0 on the command end */
	  command[value-command-1]='\0';
	  trace (TRACE_DEBUG,"pop3(): command issued :cmd [%s], value [%s]\n",command, value);
	}
    }
	
  for (cmdtype = POP3_STRT; cmdtype < POP3_END; cmdtype ++)
    if (strcasecmp(command, commands[cmdtype]) == 0) break;

  trace (TRACE_DEBUG,"pop3(): command looked up as commandtype %d",cmdtype);
	
	/* commands that are allowed to have no arguments */
  if ((value==NULL) && (cmdtype!=POP3_QUIT) && (cmdtype!=POP3_LIST) &&
      (cmdtype!=POP3_STAT) && (cmdtype!=POP3_RSET) && (cmdtype!=POP3_NOOP) &&
      (cmdtype!=POP3_LAST) && (cmdtype!=POP3_UIDL) && (cmdtype!=POP3_AUTH)) 
    {
      return pop3_error(stream,"-ERR your command does not compute\r\n");
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
	if (state!=AUTHORIZATION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");

	if (username!=NULL)
	  {
	    free(username);
	    username=NULL;
	  }

	if (username==NULL)
	  {
	    /* create memspace for username */
	    memtst((username=(char *)malloc(strlen(value)+1))==NULL);
	    strncpy (username,value,strlen(value)+1);
	  }

	fprintf ((FILE *)stream, "+OK Password required for %s\r\n",username);
	return 1;
      }

    case POP3_PASS :
      {
	if (state!=AUTHORIZATION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");

	if (password!=NULL)
	  {
	    free(password);
	    password=NULL;
	  }

	if (password==NULL)
	  {
	    /* create memspace for password */
	    memtst((password=(char *)malloc(strlen(value)+1))==NULL);
	    strncpy (password,value,strlen(value)+1);
	  }
				
	result=db_validate (username,password);
				
	switch (result)
	  {
	  case -1: return -1;
	  case 0: 
	    {
			trace (TRACE_ERROR,"pop3(): user [%s] tried to login with wrong password",
					username); 
			free (username);
			username=NULL;
			free (password);
			password=NULL;
	      return pop3_error (stream,"-ERR username/password incorrect\r\n");
	    }
	  default:
	    {
	      state = TRANSACTION;
	      /* now we're going to build up a session for this user */
	      trace(TRACE_DEBUG,"pop3(): validation ok, creating session");
	      result=db_createsession (result, &curr_session);
	      if (result==1)
		{
		  fprintf ((FILE *)stream, "+OK %s has %lu messages (%lu octets)\r\n",
			   username, curr_session.virtual_totalmessages,
			   curr_session.virtual_totalsize);
		  trace(TRACE_MESSAGE,"pop3(): user %s logged in [messages=%lu, octets=%lu]",
			username, curr_session.virtual_totalmessages,
			curr_session.virtual_totalsize);
		}
	      return result;
	    }
	  }
	return 1;
      }

    case POP3_LIST :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
			
	tmpelement=list_getstart(&curr_session.messagelst);
	if (value!=NULL) 
	  {
	    /* they're asking for a specific message */
	    while (tmpelement!=NULL)
	      {
		if (((struct message *)tmpelement->data)->messageid==atol(value) &&
		    ((struct message *)tmpelement->data)->virtual_messagestatus<2)
		  {
		    fprintf ((FILE *)stream,"+OK %lu %lu\r\n",((struct message *)tmpelement->data)->messageid,
			     ((struct message *)tmpelement->data)->msize);
		    found=1;
		  }
		tmpelement=tmpelement->nextnode;
	      }
	    if (!found)
	      return pop3_error (stream,"-ERR no such message\r\n");
	    else return (1);
	  }

				/* just drop the list */
	fprintf ((FILE *)stream, "+OK %lu messages (%lu octets)\r\n",curr_session.virtual_totalmessages,
		 curr_session.virtual_totalsize);
	if (curr_session.virtual_totalmessages>0)
	  {
	    /* traversing list */
	    while (tmpelement!=NULL)
	      {
		if (((struct message *)tmpelement->data)->virtual_messagestatus<2)
		  fprintf ((FILE *)stream,"%lu %lu\r\n",((struct message *)tmpelement->data)->messageid,
			   ((struct message *)tmpelement->data)->msize);
		tmpelement=tmpelement->nextnode;
	      }
	  }
	fprintf ((FILE *)stream,".\r\n");
	return 1;
      }

    case POP3_STAT :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
		
	fprintf ((FILE *)stream, "+OK %lu %lu\r\n",curr_session.virtual_totalmessages,
		 curr_session.virtual_totalsize);
	return 1;
      }

    case POP3_RETR : 
      {
	trace(TRACE_DEBUG,"pop3():RETR command, retrieving message");
     
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
      
	tmpelement=list_getstart(&curr_session.messagelst);
				/* selecting a message */
	trace(TRACE_DEBUG,"pop3(): RETR command, selecting message");
	while (tmpelement!=NULL)
	  {
	    if (((struct message *)tmpelement->data)->messageid==atol(value) &&
		((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
	      {
		((struct message *)tmpelement->data)->virtual_messagestatus=1;
		fprintf ((FILE *)stream,"+OK %lu octets\r\n",((struct message *)tmpelement->data)->msize); 
		return db_send_message_lines ((void *)stream, ((struct message *)tmpelement->data)->realmessageid,-2);
	      }
	    tmpelement=tmpelement->nextnode;
	  }
	return pop3_error (stream,"-ERR no such message\r\n");
      }
		
    case POP3_DELE :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
      
	tmpelement=list_getstart(&curr_session.messagelst);
	/* selecting a message */
	while (tmpelement!=NULL)
	  {
	    if (((struct message *)tmpelement->data)->messageid==atol(value) &&
		((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
	      {
		((struct message *)tmpelement->data)->virtual_messagestatus=2;
		/* decrease our virtual list fields */
		curr_session.virtual_totalsize-=((struct message *)tmpelement->data)->msize; 
		curr_session.virtual_totalmessages-=1;
		fprintf((FILE *)stream,"+OK message %lu deleted\r\n",
			((struct message *)tmpelement->data)->messageid);
		return 1;
	      }
	    tmpelement=tmpelement->nextnode;
	  }
	return pop3_error (stream,"-ERR [%s] no such message\r\n",value);
      }

    case POP3_RSET :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
      	
	tmpelement=list_getstart(&curr_session.messagelst);

	curr_session.virtual_totalsize=curr_session.totalsize;
	curr_session.virtual_totalmessages=curr_session.totalmessages;
	while (tmpelement!=NULL)
	  {
	    ((struct message *)tmpelement->data)->virtual_messagestatus=((struct message *)tmpelement->data)->messagestatus;
	    tmpelement=tmpelement->nextnode;
	  }
			
	fprintf ((FILE *)stream, "+OK %lu messages (%lu octets)\r\n",curr_session.virtual_totalmessages,
		 curr_session.virtual_totalsize);
			
	return 1;
      }

    case POP3_LAST :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
			
	tmpelement=list_getstart(&curr_session.messagelst);

	while (tmpelement!=NULL)
	  {
	    if (((struct message *)tmpelement->data)->virtual_messagestatus==0)
	      {
		/* we need the last message that has been accessed */
		fprintf ((FILE *)stream, "+OK %lu\r\n",((struct message *)tmpelement->data)->messageid-1);
		return 1;
	      }
	    tmpelement=tmpelement->nextnode;
	  }
				/* all old messages */
	fprintf ((FILE *)stream, "+OK %lu\r\n",curr_session.virtual_totalmessages);
	return 1;
      }
					
    case POP3_NOOP :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
				
	fprintf ((FILE *)stream, "+OK\r\n");
	return 1;
      }

    case POP3_UIDL :
      {
	if (state!=TRANSACTION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
			
	tmpelement=list_getstart(&curr_session.messagelst);
	if (value!=NULL) 
	  {
				/* they're asking for a specific message */
			while (tmpelement!=NULL)
			{
				if (((struct message *)tmpelement->data)->messageid==atol(value)) 
				{
					fprintf ((FILE *)stream,"+OK %lu %s\r\n",((struct message *)tmpelement->data)->messageid,
						((struct message *)tmpelement->data)->uidl);
					found=1;
				}
			tmpelement=tmpelement->nextnode;
			}
			if (!found)
				return pop3_error (stream,"-ERR no such message\r\n");
			else
				return 1;
	  }
	/* just drop the list */
	fprintf ((FILE *)stream, "+OK Some very unique numbers for you\r\n");
	if (curr_session.virtual_totalmessages>0)
	  {
	    /* traversing list */
	    while (tmpelement!=NULL)
	      {
		fprintf ((FILE *)stream,"%lu %s\r\n",((struct message *)tmpelement->data)->messageid,
			 ((struct message *)tmpelement->data)->uidl);
		tmpelement=tmpelement->nextnode;
	      }
	  }
	fprintf ((FILE *)stream,".\r\n");
	return 1;
      }

    case POP3_APOP:
      {
	if (state!=AUTHORIZATION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");

				/* find out where the md5 hash starts */
	searchptr=strstr(value," ");
				
	if (searchptr==NULL)
	  return pop3_error (stream,"-ERR your command does not compute\r\n");
			
				/* skip the space */
	searchptr=searchptr+1;
				
				/* value should now be the username */
	value[searchptr-value-1]='\0';
				
	if (strlen(searchptr)>32)
	  return pop3_error(stream,"-ERR the thingy you issued is not a valid md5 hash\r\n");

				/* create memspace for md5 hash */
	memtst((md5_apop_he=(char *)malloc(strlen(searchptr)+1))==NULL);
	strncpy (md5_apop_he,searchptr,strlen(searchptr)+1);
				
				/* create memspace for username */
	memtst((username=(char *)malloc(strlen(value)+1))==NULL);
	strncpy (username,value,strlen(value)+1);
				
	trace (TRACE_DEBUG,"pop3(): APOP auth, username [%s], md5_hash [%s]",username,
	       md5_apop_he);
				
	result=db_md5_validate (username,md5_apop_he,apop_stamp);
				
	switch (result)
	  {
	  case -1: return -1;
	  case 0: 
		  {
			trace (TRACE_ERROR,"pop3(): user [%s] tried to login with wrong password",
				username); 
			free (username);
			username=NULL;
			free (password);
			password=NULL;
		 return pop3_error(stream,"-ERR authentication attempt is invalid\r\n");
		  }
	  default:
	    {
	      state = TRANSACTION;
	      /* user seems to be valid, let's build a session */
	      trace(TRACE_DEBUG,"pop3(): validation OK, building a session for user [%s]",username);
	      result=db_createsession(result,&curr_session);
	      if (result==1)
		{
		  fprintf((FILE *)stream, "+OK %s has %lu messages (%lu octets)\r\n",
			  username, curr_session.virtual_totalmessages,
			  curr_session.virtual_totalsize);
		  trace(TRACE_MESSAGE,"pop3(): user %s logged in [messages=%lu, octets=%lu]",
			username, curr_session.virtual_totalmessages,
			curr_session.virtual_totalsize);
		}
	      return result;
	    }
	  }
	return 1;
      }	

    case POP3_AUTH:
      {
	if (state!=AUTHORIZATION)
	  return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
	fprintf  ((FILE *)stream, "+OK List of supported mechanisms\r\n.\r\n");
	return 1;
      }

	  case POP3_TOP:
			{
	if (state!=TRANSACTION)
			return pop3_error(stream,"-ERR wrong command mode, sir\r\n");
  
	/* find out how many lines they want */
	searchptr=strstr(value," ");

  /* insufficient parameters */
  if (searchptr==NULL)
    return pop3_error (stream,"-ERR your command does not compute\r\n");

        /* skip the space */
  searchptr=searchptr+1;

        /* value should now be the the message that needs to be retrieved */
  value[searchptr-value-1]='\0';

	top_lines = atol (searchptr);
	top_messageid = atol (value);

	if ((top_lines<0) || (top_messageid<1))
    return pop3_error(stream,"-ERR wrong parameter\r\n");
	
	trace(TRACE_DEBUG,"pop3():TOP command (partially) retrieving message");
     
	tmpelement=list_getstart(&curr_session.messagelst);
				/* selecting a message */
	trace(TRACE_DEBUG,"pop3(): TOP command, selecting message");
	while (tmpelement!=NULL)
	  {
	    if (((struct message *)tmpelement->data)->messageid==top_messageid &&
		((struct message *)tmpelement->data)->virtual_messagestatus<2) /* message is not deleted */
	      {
		fprintf ((FILE *)stream,"+OK %lu lines of message %lu\r\n",top_lines, top_messageid);
		return db_send_message_lines (stream, ((struct message *)tmpelement->data)->realmessageid, top_lines);
			}
	    tmpelement=tmpelement->nextnode;
	  }
	return pop3_error (stream,"-ERR no such message\r\n");

	return 1;
			}

    default : 
      {
	return pop3_error(stream,"-ERR command not understood, sir\r\n");
      }
    }
  return 1;
}
