/* implementation for pop3 commands accoording to RFC 1081 */

#include "config.h"
#include "pop3.h"
#include "dbmysql.h"

extern int state; /* tells the current negotiation state of the server */
extern char *username, *password; /* session username and password */
extern struct session curr_session;


/* allowed pop3 commands */
const char *commands [] = 
	{
	"quit", "user", "pass", "stat", "list", "retr", "dele", "noop", "last", "rset",
	"uidl"
	};

const char validchars[] = ".@ abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";


int pop3 (void *stream, char *buffer)
	{
	/* returns a 0  on a quit
	*           -1  on a failure 
	*				 1  on a success */
	char *command, *value;
	int cmdtype, found=0;
	int indx=0;
	unsigned long result;
	struct element *tmpelement;
	
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

	if ((value==NULL) && (cmdtype!=POP3_QUIT) && (cmdtype!=POP3_LIST) &&
			(cmdtype!=POP3_STAT) && (cmdtype!=POP3_RSET) && (cmdtype!=POP3_NOOP) &&
			(cmdtype!=POP3_LAST) && (cmdtype!=POP3_UIDL)) 
		{
		fprintf ((FILE *)stream,"-ERR does not compute\r\n");
		return 1;
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
					{
						fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
						return 1;
					}

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
				break;
			}

		case POP3_PASS :
			{
          if (state!=AUTHORIZATION)
					{
					fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
					return 1;
					}

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
								fprintf ((FILE *)stream, "-ERR username/password combination is incorrect\r\n");
								return 1;
								}
						default:
								{
								state = TRANSACTION;
								/* now we're going to build up a session for this user */
								trace(TRACE_DEBUG,"pop3(): validation ok, creating session");
								result=db_createsession (result, &curr_session);
								if (result==1)
									{
									fprintf ((FILE *)stream, "+OK %s has %lu messages (%lu octets).\r\n",
										username, curr_session.virtual_totalmessages,
										curr_session.virtual_totalsize);
									trace(TRACE_MESSAGE,"pop3(): user %s logged in [messages=%lu, octets=%lu]",
										username, curr_session.virtual_totalmessages,
										curr_session.virtual_totalsize);
									}
								return result;
								}
						}
				break;
			}

		case POP3_LIST :
			{
      if (state!=TRANSACTION)
			{
			 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
					return 1;
			}
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
						fprintf ((FILE *)stream,"-ERR no such message\r\n");
					return 1;
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
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
				fprintf ((FILE *)stream, "+OK %lu %lu\r\n",curr_session.virtual_totalmessages,
					curr_session.virtual_totalsize);
			return 1;
			}

		case POP3_RETR : 
			{
			trace(TRACE_DEBUG,"pop3():RETR command, retrieving message");
      if (state!=TRANSACTION)
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
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
						return db_send_message ((void *)stream, ((struct message *)tmpelement->data)->realmessageid);
						}
				tmpelement=tmpelement->nextnode;
					}
			 fprintf ((FILE *)stream,"-ERR no such message\r\n");
			 return 1;
			}
		
		case POP3_DELE :
			{
      if (state!=TRANSACTION)
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
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
					fprintf ((FILE *)stream,"-ERR [%s] no such message\r\n",value);
					return 1;
			}

		 case POP3_RSET :
			{
      if (state!=TRANSACTION)
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
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
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
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
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
	  			return 1;
				}
				fprintf ((FILE *)stream, "+OK\r\n");
				return 1;
			}
		case POP3_UIDL :
			{
			if (state!=TRANSACTION)
				{
				 fprintf ((FILE *)stream,"-ERR wrong command mode\r\n");
					return 1;
				}
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
					fprintf ((FILE *)stream,"-ERR no such message\r\n");
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

		default : 
			{
				fprintf ((FILE *)stream, "-ERR Huh? command not understood\r\n");
				return 1;
			}
		}
	return 1;
	}
