/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * pop3 daemon */

#include "pop3.h"
#include "dbmysql.h"

#define INCOMING_BUFFER_SIZE 512
#define IP_ADDR_MAXSIZE 16
#define APOP_STAMP_SIZE 255
/* default timeout for server daemon */
#define DEFAULT_SERVER_TIMEOUT 100

#ifndef SHUT_RDWR
#define SHUR_RDWR 3
#endif

/* syslog */
#define PNAME "dbmail/pop3d"

int state;
char *username=NULL, *password=NULL;
struct session curr_session;
char *myhostname;
char *apop_stamp;
char *timeout_setting;

char *buffer;
int done=1;

int resolve_client = 0;

int server_timeout;
int server_pid;

int error_count;

FILE *tx = NULL;	/* write socket */
FILE *rx = NULL;	/* read socket */

int *default_children_used;

key_t	shm_key = 0;
int shm_id;

#define SHM_ALLOC_SIZE (sizeof(int))

/* signal handler */
static void sigchld_handler (int signo)
{
  pid_t PID;
  int status;

  if ((signo == SIGALRM) && (tx!=NULL))
  {
		done=-1;
		trace (TRACE_DEBUG,"sigchld_handler(): received ALRM signal. Timeout");
		fprintf (tx,"-ERR i cannot wait forever\r\n");
		shutdown(fileno(rx),SHUT_RDWR);
		return;
  }
  else
	 if (signo == SIGCHLD)
	  {
	    while (waitpid (-1, &status, WNOHANG));
	    return;
	  }
	  else
		  trace (TRACE_STOP,"sigchld_handler(): received fatal signal [%d]",signo);
}

int handle_client(char *myhostname, int c, struct sockaddr_in adr_clnt, int *default_children_used, int is_client)
{
	 /* returns 0 when a connection was successfull
	 * returns -1 when a connection was unsuccessfull (continue in loop)
	 */

  char *theiraddress;
  char *buffer;
  
  struct hostent *clientinfo;
  
  time_t timestamp;
  time_t timeout;
  
  theiraddress=inet_ntoa(adr_clnt.sin_addr);

  memtst((clientinfo=(struct hostent *)malloc(1))==NULL);

  if (resolve_client==1)
	{
		clientinfo=gethostbyaddr((char *)&adr_clnt.sin_addr, 
		sizeof(adr_clnt.sin_addr),
			adr_clnt.sin_family);
		if (theiraddress != NULL)
			trace (TRACE_MESSAGE,"handle_client(): incoming connection from [%s (%s)]",
						theiraddress,clientinfo->h_name);
		else
		{
			trace (TRACE_ERROR,"handle_client(): error: could not get address of client"); 
			return -1;
		}
	}
	else
	{
		if (theiraddress != NULL)
			trace (TRACE_MESSAGE,"handle_client(): incoming connection from [%s]",
				theiraddress);
		else
			trace (TRACE_ERROR,"handle_client(): error: could not get address of client");
	}

	/* duplicate descriptor and open it */
	rx = fdopen (dup (c), "r"); 
	if (!rx)
	{
		/* opening of descriptor failed */
		close(c);
		return -1;
	}
	
	tx = fdopen (dup (c), "w"); 
	if (!tx)
	{
		/* opening of descriptor failed */
		close (c);
		return -1;
	}
		
	/* set stream to line buffered mode 
	* this way when we send a newline the buffer is flushed */
	/* FIXME: setlinebuf(rx);
	 * how should we set the buffer ? */

	/* connect to the database */
	if (db_connect()< 0)
	{	
		trace(TRACE_ERROR,"handle_client(): could not connect to database");
		return -1;
	}
			
	/* another default child is in use */
	if (is_client==1)
		(*default_children_used)++;
			
	/* first initiate AUTHORIZATION state */
	state = AUTHORIZATION;
		
	memtst((buffer=(char *)malloc(INCOMING_BUFFER_SIZE))==NULL);

	/* create an unique timestamp + processid for APOP authentication */
	memtst((apop_stamp=(char *)malloc(APOP_STAMP_SIZE))==NULL);
				
	timestamp=time(NULL);
				
	sprintf (apop_stamp,"<%d.%u@%s>",getpid(),timestamp,myhostname);

	/* sending greeting */
	fprintf (tx,"+OK DBMAIL server ready %s\r\n",apop_stamp);
	fflush (tx);
			
	/* no errors yet */
	error_count = 0;
			
	trace (TRACE_DEBUG,"handle_client(): setting timeout timer at %d seconds",server_timeout);	
	/* setting time for timeout counter */
	/* alarm (server_timeout); */

	/* scanning for commands */
	while ((done>0) && (buffer=fgets(buffer,INCOMING_BUFFER_SIZE,rx)))
	{
		if (feof(rx)) 
			done = -1;  /* check of client eof  */
		else 
		{
			/* alarm (server_timeout);  */
			done = pop3(tx,buffer); 
			/* alarm (server_timeout);  */
		}
	fflush (tx);
	}
					
	/* we've reached the update state */
	state = UPDATE;

	/* memory cleanup */
	free(buffer);

	if (done < 0)
	{
		trace (TRACE_ERROR,"handle_client(): timeout, connection terminated");
		fclose(tx);
		shutdown (fileno(rx), SHUT_RDWR);
		fclose(rx);
	}
	else
	{
		trace(TRACE_MESSAGE,"handle_client(): user %s logging out [message=%lu, octets=%lu]",
			username, curr_session.virtual_totalmessages,
			curr_session.virtual_totalsize);

		/* if everything went well, write down everything and do a cleanup */
		db_update_pop(&curr_session);
				
		db_disconnect(); 
	
		fclose(tx);
		shutdown (fileno(rx), SHUT_RDWR);
		fclose(rx);
	}
}

int main (int argc, char *argv[])
{
  struct sockaddr_in adr_srvr;
  struct sockaddr_in adr_clnt;
  char *myhostname;

  char *newuser, *newgroup;
  
  char *ipaddr, *port;

  char *trace_level=NULL,*trace_syslog=NULL,*trace_verbose=NULL;
  int new_level = 2, new_trace_syslog = 1, new_trace_verbose = 0;
  char *resolve_setting=NULL;
  
	

  int len_inet;
  int reuseaddress;
  int s = -1;
  int c = -1;
  int z, i; /* counters */
  int children=0;		/* child process counter */
  int defchld,maxchld; /* default children and maxchildren */

  /* open logs */
  openlog(PNAME, LOG_PID, LOG_MAIL);
  
  /* connect to the database */
  if (db_connect()< 0) 
	trace(TRACE_FATAL,"main(): could not connect to database"); 
	
  /* debug settings */
  trace_level = db_get_config_item("TRACE_LEVEL", CONFIG_EMPTY);
  trace_syslog = db_get_config_item("TRACE_TO_SYSLOG", CONFIG_EMPTY);
  trace_verbose = db_get_config_item("TRACE_VERBOSE", CONFIG_EMPTY);
  timeout_setting = db_get_config_item("POP3D_CHILD_TIMEOUT", CONFIG_EMPTY);
  resolve_setting = db_get_config_item("POP3D_IP_RESOLVE",CONFIG_EMPTY);

  if (resolve_setting)
  {
		if (strcasecmp(resolve_setting,"yes"))
				resolve_client = 1;
		else
				resolve_client = 0;
		free(resolve_setting);
  }	
  
  if (timeout_setting) 
  {
	  server_timeout = atoi(timeout_setting);
	  free (timeout_setting);
	  if (server_timeout<10)
		  trace (TRACE_STOP,"main(): POP3D_CHILD_TIMEOUT setting is insane [%d]",
				  server_timeout);
  }
  else
	  server_timeout = DEFAULT_SERVER_TIMEOUT;
  
  if (trace_level)
    {
      new_level = atoi(trace_level);
      free(trace_level);
      trace_level = NULL;
    }

  if (trace_syslog)
    {
      new_trace_syslog = atoi(trace_syslog);
      free(trace_syslog);
      trace_syslog = NULL;
    }

  if (trace_verbose)
    {
      new_trace_verbose = atoi(trace_verbose);
      free(trace_verbose);
      trace_verbose = NULL;
    }

  configure_debug(new_level, new_trace_syslog, new_trace_verbose);


  /* daemonize */
  if (fork ())
    exit (0);
  setsid ();
		
  if (fork ())
    exit (0);
		
  close (0);
  close (1);
  close (2); 

  /* reserve memory for hostname */
  memtst((myhostname=(char *)malloc(64))==NULL);
	
  /* getting hostname */
  gethostname (myhostname,64);
	
  /* set signal handler for SIGCHLD */
  signal (SIGCHLD, sigchld_handler);
  signal (SIGINT, sigchld_handler);
  signal (SIGQUIT, sigchld_handler);
  signal (SIGILL, sigchld_handler);
  signal (SIGBUS, sigchld_handler);
  signal (SIGFPE, sigchld_handler);
  signal (SIGSEGV, sigchld_handler);
  signal (SIGTERM, sigchld_handler);
  signal (SIGSTOP, sigchld_handler);
  signal (SIGALRM, sigchld_handler);

	/* allocate a shared memory segment for interprocess communication */
  shm_key = time (NULL);
  /* FIXME: should this be 666? Can't another process tap in? */
  shm_id = shmget (shm_key, SHM_ALLOC_SIZE, 0666 | IPC_CREAT);

  if (shm_id == -1)
  {
	  free (myhostname);

	  /* creation failed, we cannot continue */
	  trace (TRACE_STOP,"main(): error getting shared memory segment [%s]\n",
			  strerror(errno));
  }

  adr_srvr.sin_family = AF_INET; 
  
  s = socket (PF_INET, SOCK_STREAM, 0); /* creating the socket */
  if (s == -1 ) 
    trace (TRACE_FATAL,"main(): call socket(2) failed");

  ipaddr = db_get_config_item("POP3D_BIND_IP",CONFIG_MANDATORY);
  port = db_get_config_item("POP3D_BIND_PORT",CONFIG_MANDATORY);

  if (ipaddr != NULL)
  {
	  if (ipaddr[0] == '*') /* bind to all interfaces */
			adr_srvr.sin_addr.s_addr = htonl (INADDR_ANY); 
	  else 
		{
		   if (!inet_aton(ipaddr, &adr_srvr.sin_addr))
				trace (TRACE_FATAL, "main(): %s is not a valid ipaddress",ipaddr);
		}
  }
  else
	  trace (TRACE_FATAL,"main(): could not read bind ip from config");

  if (port != NULL)
	  adr_srvr.sin_port = htons (atoi(port));
	else
		trace (TRACE_FATAL,"main(): could not read port from config");
			
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddress, sizeof(reuseaddress));
	
  len_inet = sizeof (adr_srvr);

  z = bind (s, (struct sockaddr *)&adr_srvr, len_inet); /* bind to socket */
  if (z == -1 )
    trace (TRACE_FATAL,"main(): call bind(2) failed (could not bind to %s:%s)",ipaddr,port);

  z = listen (s, BACKLOG); /* make the socket listen */
  if (z == -1 )
    trace (TRACE_FATAL,"main(): call listen(2) failed");

  /* drop priviledges */
	trace (TRACE_MESSAGE,"main(): Dropping priviledges");		

	
  newuser = db_get_config_item("POP3D_EFFECTIVE_USER",CONFIG_MANDATORY);
  newgroup = db_get_config_item("POP3D_EFFECTIVE_GROUP",CONFIG_MANDATORY);
	
  if ((newuser!=NULL) && (newgroup!=NULL))
  {
    if (drop_priviledges (newuser, newgroup) != 0)
      trace (TRACE_FATAL,"main(): could not set uid %s, gid %s",newuser,newgroup);
    
    free(newuser);
    free(newgroup);
    newuser = NULL;
    newgroup = NULL;
  }
  else
    trace(TRACE_FATAL,"main(): newuser and newgroup should not be NULL");
	
  /* server loop */
  trace (TRACE_MESSAGE,"main(): DBmail pop3 server ready (bound to [%s:%s])",ipaddr,port);
	
	free (ipaddr);
	free (port);
  
 
 /* get child config */

  defchld = atoi(db_get_config_item("POP3D_DEFAULT_CHILD",CONFIG_MANDATORY));
  maxchld = atoi(db_get_config_item("POP3D_MAX_CHILD",CONFIG_MANDATORY));


  /* remember this pid, we're the father process */
  server_pid = getpid();

  /* attach to shared memory */
  default_children_used = (int*)shmat(shm_id, 0, 0);

  if (default_children_used == (int*) (-1))
	  trace (TRACE_FATAL,"main(): Could not attach to shared memory [%s]",
			  strerror(errno));

	/* we don't have any children yet */
	*default_children_used = 0;
	children = 0;
 
	/* create children */
	for (i=0; i<defchld; i++)
	{
		if (!fork())
			break;
		else
			children++;
	}

	/* split up in the 'server' part and the client part */
	if (getpid() != server_pid)
	{
	
		/* client part */
		for (;;)
		{
			/* wait for a connection */
			len_inet = sizeof (adr_clnt);
			c = accept (s, (struct sockaddr *)&adr_clnt,
			  &len_inet); /* incoming connection */
	
			/* failure won't cause a quit forking is too expensive */	
			if (c == -1)
			{
				trace (TRACE_ERROR,"main(): call accept(2) failed");
				continue;
			}
			handle_client(myhostname, c, adr_clnt, default_children_used, 1); 
		}

	}
	else
	{

		/* server part */
		for (;;)
		{
			sleep(1);
			/* wait for a connection. If the max number of children
				hasn't already been reached; fork */

			if (*default_children_used < defchld)
				continue;

			while (children >= maxchld)
			{
				/* we've reached a maximum number of children 
					here we wait for an exit */
				wait (NULL);
				children--;
			}

			/* wait for a connection */
			len_inet = sizeof (adr_clnt);
			c = accept (s, (struct sockaddr *)&adr_clnt,
			  &len_inet); /* incoming connection */
	
			/* failure won't cause a quit forking is too expensive */	
			if (c == -1)
			{
				trace (TRACE_ERROR,"main(): call accept(2) failed");
				continue;
			}

			if (fork())
			{
				children++;
				continue;
			}
			else
			{
				handle_client(myhostname, c, adr_clnt, default_children_used, 0);
				return 0;
			}
		}
	}
	return 0;
}		
