/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * pop3 daemon */

#include "pop3.h"
#include "dbmysql.h"
#include "debug.h"

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

int resolve_client;

int server_timeout;
int server_pid;

int error_count;

FILE *tx = NULL;	/* write socket */
FILE *rx = NULL;	/* read socket */

int *default_children;  /* # of default children in use */
int defchld;            /* # of default children as specified in conf */
int total_children = 0;

int done;

/* 'dcu' stands for 'default children used' */
/* 'dcp' stands for 'default children pids' */
key_t shmkey_dcu=0,shmkey_dcp=0;
int shmid_dcu,shmid_dcp;

#define SHM_ALLOC_SIZE (sizeof(int))

/* list of PID's of the default-children */
pid_t *default_child_pids;
int n_default_children;


/* signal handler */
static void signal_handler (int signo, siginfo_t *info, void *data)
{
  pid_t PID;
  int status,i;

  if ((signo == SIGALRM) && tx && rx)
  {
    done=-1;
    trace (TRACE_DEBUG,"signal_handler(): received ALRM signal. Timeout");
    fprintf (tx,"-ERR i cannot wait forever\r\n");
    fflush (tx);
    shutdown(fileno(tx),SHUT_RDWR);
    shutdown(fileno(rx),SHUT_RDWR);
    tx = NULL;
    rx = NULL;
    return;
  }
  else
    if (signo == SIGCHLD)
      {
	trace (TRACE_DEBUG,"signal_handler(): sigCHLD, cleaning up zombies");
	do {
	  PID = waitpid (info->si_pid,&status,WNOHANG);
	  sleep (1);
	} while ( PID != -1);

	trace (TRACE_DEBUG,"signal_handler(): sigCHLD, cleaned");
	return;
      }
    else
      {
	/* reset the entry of this process if it is a default child (so it can be restored) */
	for (i=0; i<defchld; i++)
	  if (getpid() == default_child_pids[i])
	    {
	      default_child_pids[i] = 0;
	      (*default_children)--;
	      break;
	    }

	trace (TRACE_STOP,"signal_handler(): received fatal signal [%d]",signo);
      }
}

int handle_client(char *myhostname, int c, struct sockaddr_in adr_clnt)
{
	 /* returns 0 when a connection was successfull
	 * returns -1 when a connection was unsuccessfull (continue in loop)
	 */

  char *theiraddress;
  char *buffer;
  
  struct hostent *clientinfo;
  
  time_t timestamp;

  /* reset */
  done = 1;
  
  theiraddress=inet_ntoa(adr_clnt.sin_addr);

  if (resolve_client==1)
    {
      clientinfo=gethostbyaddr((char *)&adr_clnt.sin_addr, 
			       sizeof(adr_clnt.sin_addr),
			       adr_clnt.sin_family);

		
	  if (theiraddress != NULL)
		trace (TRACE_MESSAGE,"handle_client(): incoming connection from [%s (%s)]",
			theiraddress, clientinfo ? (clientinfo->h_name ? clientinfo->h_name: "NULL")  : "Lookup failed");
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
  setlinebuf(rx);
  setlinebuf(tx); 

  /* connect to the database */
  if (db_connect()< 0)
    {	
      trace(TRACE_ERROR,"handle_client(): could not connect to database");
      return -1;
    }
			
  /* first initiate AUTHORIZATION state */
  state = AUTHORIZATION;
		
  memtst((buffer=(char *)my_malloc(INCOMING_BUFFER_SIZE))==NULL);

	/* create an unique timestamp + processid for APOP authentication */
  memtst((apop_stamp=(char *)my_malloc(APOP_STAMP_SIZE))==NULL);
				
  timestamp=time(NULL);
				
  sprintf (apop_stamp,"<%d.%lu@%s>",getpid(),timestamp,myhostname);

  /* sending greeting */
  fprintf (tx,"+OK DBMAIL pop3 server ready %s\r\n",apop_stamp);
  fflush (tx);
			
	/* no errors yet */
  error_count = 0;
			
  trace (TRACE_DEBUG,"handle_client(): setting timeout timer at %d seconds",server_timeout);	
  /* setting time for timeout counter */
  alarm (server_timeout); 

	/* scanning for commands */
  while ((done>0) && (buffer=fgets(buffer,INCOMING_BUFFER_SIZE,rx)))
    {
      if (feof(rx)) 
	done = -1;  /* check of client eof  */
      else 
	{
	  alarm (0);  
	  /* handle pop3 commands */
	  done = pop3(tx,buffer); 
	  /* cleanup the buffer */
	  memset (buffer, '\0', INCOMING_BUFFER_SIZE);
	  /* reset the timeout counter */
	  alarm (server_timeout); 
	}
      fflush (tx);
    }
					
  /* we've reached the update state */
  state = UPDATE;

  /* memory cleanup */
  my_free(buffer);
  buffer = NULL;
  my_free(apop_stamp);
  apop_stamp = NULL;

  if (done == -3)
    {
      trace (TRACE_ERROR,"handle_client(): alert: possible flood attempt, closing connection.");
      fclose (tx);
      shutdown (fileno(rx), SHUT_RDWR);
      fclose(rx);
    }
  else if (done < 0)
    {
      trace (TRACE_ERROR,"handle_client(): client EOF, connection terminated");
      fclose(tx);
      shutdown (fileno(rx), SHUT_RDWR);
      fclose(rx);
    }
  else
    {
      if (username == NULL)
	trace (TRACE_ERROR,"handle_client(): error, uncomplete session");
      else
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

  /* clean this session */
  db_session_cleanup(&curr_session);
	
  if (username!=NULL)
    {
      /* username cleanup */
      my_free(username);
      username=NULL;
    }

  if (password!=NULL)
    {
      /* password cleanup */
      my_free(password);
      password=NULL;
    }

		
  /* reset timers */
  alarm (0); 

  return 0;
}


int main (int argc, char *argv[])
{
  struct sockaddr_in adr_srvr;
  struct sockaddr_in adr_clnt;
  struct sigaction act;
  char myhostname[64];

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
  int maxchld; /* maxchildren */

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
		if (strcasecmp(resolve_setting,"yes")==0)
				resolve_client = 1;
		else
				resolve_client = 0;
		my_free(resolve_setting);
		resolve_setting = NULL;
  }	
  
  if (timeout_setting) 
  {
	  server_timeout = atoi(timeout_setting);
	  my_free (timeout_setting);
	  if (server_timeout<10)
		  trace (TRACE_STOP,"main(): POP3D_CHILD_TIMEOUT setting is insane [%d]",
				  server_timeout);
	  timeout_setting = NULL;
  }
  else
	  server_timeout = DEFAULT_SERVER_TIMEOUT;
  
  if (trace_level)
    {
      new_level = atoi(trace_level);
      my_free(trace_level);
      trace_level = NULL;
    }

  if (trace_syslog)
    {
      new_trace_syslog = atoi(trace_syslog);
      my_free(trace_syslog);
      trace_syslog = NULL;
    }

  if (trace_verbose)
    {
      new_trace_verbose = atoi(trace_verbose);
      my_free(trace_verbose);
      trace_verbose = NULL;
    }

  configure_debug(new_level, new_trace_syslog, new_trace_verbose);


  /* daemonize */
  if (fork ())
    exit (0);
		
  
  close (0);
  close (1);
  close (2); 
  close (3);

  /* getting hostname */
  gethostname (myhostname,64);
  myhostname[63] = 0; /* make sure string is terminated */
	
  /* init & install signal handlers */
  memset(&act, 0, sizeof(act));

  act.sa_sigaction = signal_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_SIGINFO;

  sigaction(SIGCHLD, &act, 0);
/*  sigaction(SIGPIPE, &act, 0);*/
  sigaction(SIGINT, &act, 0);
  sigaction(SIGQUIT, &act, 0);
  sigaction(SIGILL, &act, 0);
  sigaction(SIGBUS, &act, 0);
  sigaction(SIGFPE, &act, 0);
  sigaction(SIGSEGV, &act, 0);
  sigaction(SIGTERM, &act, 0);
  sigaction(SIGSTOP, &act, 0);
  sigaction(SIGALRM, &act, 0);

  adr_srvr.sin_family = PF_INET; 
  
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
    
    my_free(newuser);
    my_free(newgroup);
    newuser = NULL;
    newgroup = NULL;
  }
  else
    trace(TRACE_FATAL,"main(): newuser and newgroup should not be NULL");

  /* get child config */
  defchld = atoi(db_get_config_item("POP3D_DEFAULT_CHILD",CONFIG_MANDATORY));
  maxchld = atoi(db_get_config_item("POP3D_MAX_CHILD",CONFIG_MANDATORY));


  /* getting shared memory children counter */
  shmkey_dcu = time (NULL); /* get an unique key */
  shmid_dcu = shmget (shmkey_dcu, sizeof (int), 0666 | IPC_CREAT);

  shmkey_dcp = time (NULL)+1; /* get an unique key */
  shmid_dcp = shmget (shmkey_dcp, sizeof (pid_t) * defchld, 0666 | IPC_CREAT);

  if (shmid_dcu == -1 || shmid_dcp == -1)
    trace (TRACE_FATAL,"main(): could not allocate shared memory");


  /* server loop */
  trace (TRACE_MESSAGE,"main(): DBmail pop3 server ready (bound to [%s:%s])",ipaddr,port);
	
  my_free (ipaddr);
  my_free (port);
  

  /* remember this pid, we're the father process */
  server_pid = getpid();
  

  /* attach to shared mem */
  default_children = (int *)shmat(shmid_dcu, 0, 0);
  if (default_children == (int *)-1)
    trace (TRACE_FATAL,"main(): could not attach to shared memory block (dcu)");

  default_child_pids = (pid_t*)shmat(shmid_dcp, 0, 0);
  if (default_child_pids == (pid_t*)-1)
    trace (TRACE_FATAL,"main(): could not attach to shared memory block (dcp)");


  /* we don't have any children yet so no active children neither */
  *default_children = 0;
	
  /* spawn the default children */
  for (i=0; i<defchld; i++)
    {
      if (!fork())
	{
	  default_child_pids[i] = getpid();
	  break;
	}
      else
	{
	  while (default_child_pids[i] == 0) 
	    {
	      trace(TRACE_DEBUG, "main(): waiting for child to catch up...\n");
	      sleep(1); /* wait until child has catched up */
	    }
	  total_children++;
	}
    }

  /* this infinite loop is needed for killed default-children:
   * they should re-enter at the following if-statement
   */

  for ( ;; )
    {
      /* split up in the 'server' part and the client part */

      /* 
       * Client loop 
       */
      if (getpid() != server_pid)
	{
	  for (;;)
	    {
	      /* wait for a connection */
	      len_inet = sizeof (adr_clnt);
	      c = accept (s, (struct sockaddr *)&adr_clnt,
			  &len_inet); /* incoming connection */
	
	      /* failure won't cause a quit forking is too expensive */	
	      if (c == -1)
		{
		  trace (TRACE_ERROR,"main(): call accept(2) failed [%s]", strerror(errno));
		  continue;
		}
		
	      (*default_children)++;		
			
	      handle_client(myhostname, c, adr_clnt);
		
	      (*default_children)--;
	    }
	}
      else
	for (;;)
	  {
	    /* check if a default-child has died 
	     * (it's entry has been set to zero in default_child_pids[]) 
	     */
	    for (i=0; i<defchld && default_child_pids[i]; i++) ;
	      
	    if (i<defchld)
	      {
		/* def-child has died, re-create */
		if (!fork())
		  {
		    default_child_pids[i] = getpid();
		    break;  
		    /* after this break the if (getpid() == ss_server_pid) will be re-executed */
		  }
	      }
	    
	    if (*default_children < defchld)
	      {
		sleep (1); /* don't hog cpu */
		continue;
	      }

	    while (total_children >= maxchld)
	      {
		sleep (1); /* don't hog cpu */
		wait (NULL); /* wait for children to finish */
		total_children--;
	      }

	    /* wait for a connection */
	    len_inet = sizeof (adr_clnt);
	    c = accept (s, (struct sockaddr *)&adr_clnt,
			&len_inet); /* incoming connection */
	
	    /* failure won't cause a quit forking is too expensive */	
	    if (c == -1)
	      {
		trace (TRACE_ERROR,"main(): call accept(2) failed [%s]", strerror(errno));
		continue;
	      }
		
	    if (fork())
	      {
		total_children++;
		continue;
	      }
	    else
	      {
		/* handle client connection */
		handle_client(myhostname, c, adr_clnt);
		exit(0);
	      }
	  }
    }

  /* nothing will ever get here */
  return 0;
}		
