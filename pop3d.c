/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * pop3 daemon */

#include <string.h>
#include "pop3.h"
#include "db.h"
#include "debug.h"
#include "auth.h"
#include <errno.h>
#include "config.h"

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

char *configFile = "dbmail.conf";

/* set up database login data */
extern field_t _db_host;
extern field_t _db_db;
extern field_t _db_user;
extern field_t _db_pass;


int state;
char *username=NULL, *password=NULL;
struct session curr_session;
char *myhostname;
char *apop_stamp;
char *timeout_setting;


char *buffer;

int resolve_client;
int pop_before_smtp = 0;
char *client_ip = NULL;

int server_timeout;
int server_pid;

int error_count;

FILE *tx = NULL;	/* write socket */
FILE *rx = NULL;	/* read socket */

int *default_children;  /* # of default children in use */
int defchld;            /* # of default children as specified in conf */
int maxchld;            /* max. # of children */
int total_children = 0;
pid_t *xtrachild_pids;

int done;

/* 'dcu' stands for 'default children used' */
/* 'dcp' stands for 'default children pids' */
key_t shmkey_dcu=0,shmkey_dcp=0;
int shmid_dcu,shmid_dcp;

key_t shmkey_xtrachilds=0;
int shmid_xtrachilds;

#define SHM_ALLOC_SIZE (sizeof(int))

/* list of PID's of the default-children */
pid_t *default_child_pids;
int n_default_children;


/* signal handler */
static void signal_handler (int signo, siginfo_t *info, void *data)
{
  pid_t PID;
  int status,i;
  int saved_errno = errno; /* save error status */

  if (signo == SIGUSR1)
    {
      trace(TRACE_DEBUG, "signal_handler(): caught SIGUSR1, assuming ping");
      errno = saved_errno;
      return;
    }

  if (signo == SIGALRM)
  {
    done = -1;
    trace (TRACE_DEBUG,"signal_handler(): received ALRM signal. Timeout");

    if (tx)
      {
	fprintf (tx,"-ERR i cannot wait forever\r\n");
	fflush (tx);
	shutdown(fileno(tx),SHUT_RDWR);
      }

    if (rx)
      shutdown(fileno(rx),SHUT_RDWR);

    tx = NULL;
    rx = NULL;

    errno = saved_errno;
    return;
  }
  else
    if (signo == SIGCHLD)
      {
	trace (TRACE_DEBUG,"signal_handler(): sigCHLD, cleaning up zombies for PID %d",info->si_pid);

	PID = waitpid (info->si_pid, &status, WNOHANG | WUNTRACED);

	/* a default child that as done a normal exit() or caught a fatal signal will have reset 
	 * it's own entry in default_child_pids[]
	 * an extra check is needed for uncaught signals 
	 */
	if (WIFSIGNALED(status))
	  {
	    /* child died because of an uncaught signal, check children */
	    
	    for (i=0; i<defchld; i++)
	      {
		if (default_child_pids[i] == 0) /* only allow real PID's (> 0) */
		  continue;

		trace(TRACE_DEBUG,"signal_handler(): pinging to [%u]\n",default_child_pids[i]);

		if (kill(default_child_pids[i], 0) == -1 && errno == ESRCH)
		  {
		    /* this child no longer exists */
		    trace(TRACE_DEBUG, "signal_handler(): cleaning up PID %u", default_child_pids[i]);
		    default_child_pids[i] = 0;
		    (*default_children)--;
		    break;
		  }
	      }
	  }

	/* check if an extra child died. if so, reset its entry in xtra_child_pids[] */
	for (i=0; i<maxchld; i++)
	  {
	    if (xtrachild_pids[i] == PID)
	      {
		xtrachild_pids[i] = 0;
		total_children--;
		break;
	      }
	  }	    
	
	trace (TRACE_DEBUG,"signal_handler(): sigCHLD, cleaned");
	errno = saved_errno;
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
  int cnt;
  
  struct hostent *clientinfo;
  
  time_t timestamp;

  /* reset */
  done = 1;
  
  theiraddress = inet_ntoa(adr_clnt.sin_addr);
  client_ip = theiraddress;

  if (resolve_client==1)
    {
      clientinfo=gethostbyaddr((char *)&adr_clnt.sin_addr, 
			       sizeof(adr_clnt.sin_addr),
			       adr_clnt.sin_family);

		
      if (theiraddress != NULL)
	trace (TRACE_MESSAGE,"handle_client(): incoming connection from [%s (%s)]",
	       theiraddress, clientinfo ? 
	       (clientinfo->h_name ? clientinfo->h_name: "NULL")  : "Lookup failed");
      else
	{
	  trace (TRACE_ERROR,"handle_client(): error: could not get address of client"); 
	  close(c);
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
  rx = fdopen(dup(c), "r"); 
  if (!rx)
    {
      /* opening of descriptor failed */
      close(c);
      return -1;
    }
	
  tx = fdopen(c, "w"); 
  if (!tx)
    {
      /* opening of descriptor failed */
      fclose(rx);
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
      fclose(rx);
      fclose(tx);
      return -1;
    }

  if (auth_connect()< 0)
    {	
      trace(TRACE_ERROR,"handle_client(): could not connect to user database");
      fclose(rx);
      fclose(tx);
      db_disconnect();
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
  while (done>0)
    {
      memset(buffer, 0, INCOMING_BUFFER_SIZE);
      for (cnt=0; cnt < INCOMING_BUFFER_SIZE-1; cnt++)
	{
	  do
	    {
	      clearerr(rx);
	      fread(&buffer[cnt], 1, 1, rx);
	    } while (ferror(rx) && errno == EINTR);

	  if (buffer[cnt] == '\n' || feof(rx) || ferror(rx))
	    {
	      buffer[cnt+1] = '\0';
	      break;
	    }
	}
      
      if (feof(rx) || ferror(rx)) 
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
      if (tx)
	{
	  fclose (tx);
	  tx = NULL;
	}
      if (rx)
	{
	  shutdown (fileno(rx), SHUT_RDWR);
	  fclose(rx);
	  rx = NULL;
	}

      db_disconnect(); 
      auth_disconnect();
    }
  else if (done < 0)
    {
      trace (TRACE_ERROR,"handle_client(): client EOF, connection terminated");
      if (tx)
	{
	  fclose(tx);
	  tx = NULL;
	}
      if (rx)
	{
	  shutdown (fileno(rx), SHUT_RDWR);
	  fclose(rx);
	  rx = NULL;
	}

      db_disconnect(); 
      auth_disconnect();
    }
  else
    {
      if (username == NULL)
	trace (TRACE_ERROR,"handle_client(): error, uncomplete session");
      else
	trace(TRACE_MESSAGE,"handle_client(): user %s logging out [message=%llu, octets=%llu]",
	      username, curr_session.virtual_totalmessages,
	      curr_session.virtual_totalsize);

      /* if everything went well, write down everything and do a cleanup */
      db_update_pop(&curr_session);
      db_disconnect(); 
      auth_disconnect();

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
  __debug_dumpallocs();

  return 0;
}


int main (int argc, char *argv[])
{
  struct sockaddr_in adr_srvr;
  struct sockaddr_in adr_clnt;
  struct sigaction act;
  char myhostname[64];
  struct list popItems, sysItems;
  field_t val, newuser, newgroup;
  pid_t childpid = 0;

  int len_inet;
  int reuseaddress;
  int s = -1;
  int c = -1;
  int z, i, j; /* counters */
  pid_t deadchildpid;
  int n_connects = 0,n_max_connects=0;

  /* open logs */
  openlog(PNAME, LOG_PID, LOG_MAIL);

  if (argc >= 2 && strcmp(argv[1], "-f") == 0)
    {
      if (!argv[2])
	trace(TRACE_FATAL,"main(): no file specified for -f option. Fatal.");

      configFile = argv[2];
    }
  
  ReadConfig("POP", configFile, &popItems);
  ReadConfig("DBMAIL", configFile, &sysItems);
  SetTraceLevel(&popItems);
  GetDBParams(_db_host, _db_db, _db_user, _db_pass, &sysItems);

  /* connect to the database */
  if (db_connect()< 0) 
    trace(TRACE_FATAL,"main(): could not connect to database"); 
	
  GetConfigValue("TIMEOUT", &popItems, val);
  server_timeout = atoi(val);
  if (server_timeout < 10)
    server_timeout = DEFAULT_SERVER_TIMEOUT;

  GetConfigValue("RESOLVE_IP", &popItems, val);
  if (strcasecmp(val,"yes")==0)
    resolve_client = 1;
  else
    resolve_client = 0;

  GetConfigValue("POP_BEFORE_SMTP", &popItems, val);
  if (strcasecmp(val,"yes") == 0)
    pop_before_smtp = 1;
  else
    pop_before_smtp = 0;


  GetConfigValue("BINDIP", &popItems, val);
  if (val[0] == '\0')
    trace(TRACE_FATAL,"main(): no IP to bind to specified. Fatal.");

  if (val[0] == '*') /* bind to all interfaces */
      adr_srvr.sin_addr.s_addr = htonl (INADDR_ANY); 
  else 
    {
      if (!inet_aton(val, &adr_srvr.sin_addr))
	trace (TRACE_FATAL, "main(): [%s] is not a valid ipaddress", val);
    }

  GetConfigValue("PORT", &popItems, val);
  if (val[0] != '\0')
    adr_srvr.sin_port = htons (atoi(val));
  else
    trace (TRACE_FATAL,"main(): no PORT in config. fatal.");


  /* get child config */
  GetConfigValue("NCHILDREN", &popItems, val);
  if (val[0] == '\0')
    trace(TRACE_FATAL, "main(): no value for NCHILDREN in config. Fatal.");
  defchld = atoi(val);

  GetConfigValue("MAXCHILDREN", &popItems, val);
  if (val[0] == '\0')
    trace(TRACE_FATAL, "main(): no value for MAXCHILDREN in config. Fatal.");
  maxchld = atoi(val);

  GetConfigValue("MAXCONNECTS", &popItems, val);
  n_max_connects = atoi(val);
  if (n_max_connects <= 1)
    n_max_connects = POP3_DEF_MAXCONNECT;

  GetConfigValue("EFFECTIVE_USER", &popItems, newuser);
  GetConfigValue("EFFECTIVE_GROUP", &popItems, newgroup);
  if (!newuser[0] || !newgroup[0])
    trace(TRACE_FATAL,"main(): no newuser and newgroup in config");


  /* daemonize */
  if (fork ())
    exit (0);
		
  
  close (fileno(stdin));
  close (fileno(stdout));
  close (fileno(stderr)); 


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
  sigaction(SIGUSR1, &act, 0);

  adr_srvr.sin_family = PF_INET; 
  
  s = socket (PF_INET, SOCK_STREAM, 0); /* creating the socket */
  if (s == -1 ) 
    trace (TRACE_FATAL,"main(): call socket(2) failed");

			
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuseaddress, sizeof(reuseaddress));
	
  len_inet = sizeof (adr_srvr);

  z = bind (s, (struct sockaddr *)&adr_srvr, len_inet); /* bind to socket */
  if (z == -1 )
    trace (TRACE_FATAL,"main(): call bind(2) failed");

  z = listen (s, BACKLOG); /* make the socket listen */
  if (z == -1 )
    trace (TRACE_FATAL,"main(): call listen(2) failed");

  /* drop priviledges */
  trace (TRACE_MESSAGE,"main(): Dropping priviledges");		

  if (drop_priviledges (newuser, newgroup) != 0)
    trace (TRACE_FATAL,"main(): could not set uid %s, gid %s",newuser,newgroup);
  
  /* OK all config read, close dbase connection */

  /* getting shared memory children counter */
  shmkey_dcu = time (NULL); /* get an unique key */
  shmid_dcu = shmget (shmkey_dcu, sizeof (int), 0666 | IPC_CREAT);

  shmkey_dcp = time (NULL)+1; /* get an unique key */
  shmid_dcp = shmget (shmkey_dcp, sizeof (pid_t) * defchld, 0666 | IPC_CREAT);

  shmkey_xtrachilds = time (NULL)+2; /* get an unique key */
  shmid_xtrachilds = shmget (shmkey_xtrachilds, sizeof (pid_t) * maxchld, 0666 | IPC_CREAT);

  if (shmid_dcu == -1 || shmid_dcp == -1 || shmid_xtrachilds == -1)
    trace (TRACE_FATAL,"main(): could not allocate shared memory: %s",strerror(errno));


  /* server loop */
  trace (TRACE_MESSAGE,"main(): DBmail pop3 server ready");
	

  /* remember this pid, we're the father process */
  server_pid = getpid();
  

  /* attach to shared mem */
  default_children = (int *)shmat(shmid_dcu, 0, 0);
  if (default_children == (int *)-1)
    trace (TRACE_FATAL,"main(): could not attach to shared memory block (dcu)");

  default_child_pids = (pid_t*)shmat(shmid_dcp, 0, 0);
  if (default_child_pids == (pid_t*)-1)
    trace (TRACE_FATAL,"main(): could not attach to shared memory block (dcp)");

  xtrachild_pids = (pid_t*)shmat(shmid_xtrachilds, 0, 0);
  memset(xtrachild_pids, 0, sizeof(pid_t)*maxchld);
  if (xtrachild_pids == (pid_t*)-1)
    trace (TRACE_FATAL,"main(): could not attach to shared memory block (xtrachild_pids)");


  /* we don't have any children yet so no active children neither */
  *default_children = 0;
	
  /* spawn the default children */
  for (i=0; i<defchld; i++)
    {
      switch ( (childpid = fork()) )
	{
	case 0:
	  n_connects = 0;
	  break;
	case -1:
	  perror("main(): fork failed");
	  break;
	default:
	  default_child_pids[i] = childpid;
	  total_children++;
	}

      if (childpid == 0)
	break;
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
	      n_connects++;

	      handle_client(myhostname, c, adr_clnt);
		
	      (*default_children)--;

	      if (n_connects >= n_max_connects)
		{
		  trace(TRACE_ERROR,"Maximum # of connections reached, committing suicide...\n");
		  for (i=0; i<defchld; i++)
		    if (default_child_pids[i] == getpid())
		      {
			sleep(1); /* allow father process to catch up */
			default_child_pids[i] = 0;
			exit(0);
		      }
		}
	    }
	}
      else
	for (;;)
	  {
	    /* check if default-child have died 
	     * (their entries have been set to zero in default_child_pids[]) 
	     */
	    for (i=0; i<defchld; i++)
	      {
		if (default_child_pids[i] == 0 || (kill(default_child_pids[i],0) ==  -1 && errno == ESRCH) ) 
		  {
		    /* def-child has died, re-create */
		    if (!fork())
		      {
			default_child_pids[i] = getpid();
			n_connects = 0;
			break;  
			/* after this break the if (getpid() == server_pid) will be re-executed */
		      }
		    else
		      {
			j=0;
			while (default_child_pids[i] == 0 && ++j<100) usleep(100);
		      }
		  }
	      }

	    if (getpid() != server_pid)
	      break;
	    

	    while ((deadchildpid = waitpid (-1, NULL, WNOHANG | WUNTRACED)) > 0)
	      {
		trace(TRACE_DEBUG,"main(): got %ld from waitpid",deadchildpid);
		for (i=0; i<maxchld; i++)
		  {
		    if (xtrachild_pids[i] == deadchildpid)
		      {
			xtrachild_pids[i] = 0;
			total_children--;
			break;
		      }
		  }

		for (i=0; i<defchld; i++)
		  {
		    if (default_child_pids[i] == deadchildpid)
		      {
			trace(TRACE_DEBUG, "main(): got dead default child");
			default_child_pids[i] = 0;
		      }
		  }
	      }

	    /* clean up list */
	    for (i=0; i<maxchld; i++)
	      if (xtrachild_pids[i] && 
		  waitpid(xtrachild_pids[i], NULL, WNOHANG | WUNTRACED) == -1 &&
		  errno == ECHILD)
		{
		  xtrachild_pids[i] = 0;
		  total_children--;
		}

	    if ((*default_children) < defchld)
	      {
		/* not all the def-childs are in use, continue */
		sleep(1); /* don't hog cpu */
		continue;
	      }

	    if (total_children >= maxchld)
	      {
		sleep(1);
		continue;
	      }

	    /* wait for a connection */
	    len_inet = sizeof (adr_clnt);
	    c = accept (s, (struct sockaddr *)&adr_clnt,
			&len_inet); /* incoming connection */
	
	    /* failure won't cause a quit forking is too expensive */	
	    if (c == -1 && errno != EINTR) /* dont show failure for EINTR */
	      {
		trace (TRACE_ERROR,"main(): call accept(2) failed [%s]", strerror(errno));
		continue;
	      }

	    for (i=0; i<maxchld; i++)
	      if (!xtrachild_pids[i])
		break;

	    if (i == maxchld)
	      {
		/* no free places ?? */
		close(c);
		continue;
	      }

	    if ( (childpid = fork()) )
	      {
		if (childpid != -1)
		  {
		    xtrachild_pids[i] = childpid; /* save pid */
		    total_children++;
		  }
		else
		  {
		    trace(TRACE_ERROR, "main(): forked failed [%s]", strerror(errno));
		  }

		close(c);
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
