/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * serverservice.c
 *
 * implements server functionality
 *
 */


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include "serverservice.h"
#include "debug.h"
#include <fcntl.h>


#define LOG_USERS 1

/* error data */
#define SS_ERROR_MSG_LEN 100
char  ss_error_msg[SS_ERROR_MSG_LEN];

/* shared memory */
/* 'dcu' stands for 'default children used' */
key_t shm_key_dcu = 0;
int   shm_id_dcu;

#define SHM_DCU_ALLOC_SIZE (sizeof(int))

/* 'dcp' stands for 'default children pids' */
key_t shm_key_dcp = 0;
int   shm_id_dcp;

#define SHM_ONE_DCP_ALLOC_SIZE (sizeof(pid_t))

/* xc stands for 'xtra child'*/
key_t shm_key_xc = 0;
int   shm_id_xc;

#define SHM_ONE_XC_ALLOC_SIZE (sizeof(pid_t))

/* list of PID's of the default-children & extra spawned children */
pid_t *default_child_pids,*xtrachild_pids;
int n_default_children;

/* the client being processed, this data is global 
 * to be able to see it in the sighandler
 */
ClientInfo client;
int *ss_n_default_children_used;
void (*the_client_cleanup)(ClientInfo *ci);
int server_timeout;

/* the message displayed when closing a connection due to timeout */
const char *ss_timeout_msg;

/* transmit buffer */
#define TXBUFSIZE 1024
char txbuf[TXBUFSIZE];


char *SS_GetErrorMsg()
{
  return ss_error_msg;
}

void SS_sighandler(int sig, siginfo_t *info, void *data);


int SS_MakeServerSock(const char *ipaddr, const char *port, int default_children, int max_children,
		      int timeout, const char *timeout_msg)
{
  int sock,r,len;
  struct sockaddr_in saServer;
  int so_reuseaddress;
  
  /* make a tcp/ip socket */
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    {
      /* error */
      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_MakeServerSock(): cannot create socket.");
      return -1;
    }

  /* make an (socket)address */
  memset(&saServer, 0, sizeof(saServer));

  saServer.sin_family = AF_INET;
  saServer.sin_port = htons(atoi(port));

  if (ipaddr[0] == '*')
    saServer.sin_addr.s_addr = htonl(INADDR_ANY); 
  else
    {
      r = inet_aton(ipaddr, &saServer.sin_addr);
      if (!r)
	{
	  /* error */
	  snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_MakeServerSock(): "
		   "wrong ip-address specified");
	  close(sock);
	  return -1;
	}
    }

  /* set socket option: reuse address */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

  /* bind the address */
  len = sizeof(saServer);
  r = bind(sock, (struct sockaddr*)&saServer, len);

  if (r == -1)
    {
      /* fout */
      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_MakeServerSock(): "
	      "cannot bind address to socket.");
      close(sock);
      return -1;
    }


  r = listen(sock, SS_BACKLOG);
  if (r == -1)
    {
      /* error */
      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_MakeServerSock(): socket cannot listen.");
      close(sock);
      return -1;
    }
  

  /* now allocate shared memory segments */
  shm_key_dcu = time(NULL);
  shm_id_dcu = shmget(shm_key_dcu, SHM_DCU_ALLOC_SIZE, 0666 | IPC_CREAT);

  if (shm_id_dcu == -1)
    {
      snprintf(ss_error_msg, SS_ERROR_MSG_LEN, "SS_MakeServerSock(): error getting shared "
	       "memory segment [%s] (dcu)\n", strerror(errno));
      close(sock);
      return -1;
    }

  shm_key_dcp = time(NULL)+1;
  shm_id_dcp = shmget(shm_key_dcp, SHM_ONE_DCP_ALLOC_SIZE * default_children, 0666 | IPC_CREAT);

  if (shm_id_dcp == -1)
    {
      snprintf(ss_error_msg, SS_ERROR_MSG_LEN, "SS_MakeServerSock(): error getting shared "
	       "memory segment [%s] (dcp)\n", strerror(errno));
      close(sock);
      return -1;
    }

  shm_key_xc = time(NULL)+2;
  shm_id_xc = shmget(shm_key_xc, SHM_ONE_XC_ALLOC_SIZE * max_children, 0666 | IPC_CREAT);

  if (shm_id_xc == -1)
    {
      snprintf(ss_error_msg, SS_ERROR_MSG_LEN, "SS_MakeServerSock(): error getting shared "
	       "memory segment [%s] (xc)\n", strerror(errno));
      close(sock);
      return -1;
    }

  n_default_children = default_children;

  /* save timeout value */
  server_timeout = (timeout < SS_MINIMAL_TIMEOUT) ? SS_DEFAULT_TIMEOUT : timeout;
  ss_timeout_msg = timeout_msg;

  snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"Server socket has been created.\n");
  return sock;
}


/*
 * SS_WaitAndProcess()
 * 
 * Wait for clients, let 'm log in and call a user-specified handler to 
 * process the requests.
 *
 */
int SS_WaitAndProcess(int sock, int default_children, int max_children, int daemonize,
		      int max_connects,
		      int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*),
		      void (*ClientCleanup)(ClientInfo*))
{
  struct sockaddr_in saClient;
  int csock,len,i,j;
  struct sigaction act;
  int ss_nchildren=0;
  pid_t ss_server_pid=0; /* PID of the great father process */
  pid_t deadchildpid;
  int n_connects=0;

  if (n_default_children != default_children)
    trace(TRACE_FATAL,"SS_WaitAndProcess(): incompatible number of default-children specified.\n");

  /* cleanup call in case of a signal */
  the_client_cleanup = ClientCleanup;

  /* init & install signal handlers */
  memset(&act, 0, sizeof(act));

  act.sa_sigaction = SS_sighandler;
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
  sigaction(SIGUSR1, &act, 0); /* our 'ping' signal */

  /* daemonize */
  if (daemonize)
    {
      if (fork())
	exit(0);
      setsid();

      if (fork())
	exit(0);
    }

  /* this process will be the father process, remember pid */
  ss_server_pid = getpid();


  /* attach to shm */
  ss_n_default_children_used = (int*)shmat(shm_id_dcu, 0, 0);
  if (ss_n_default_children_used == (int*)(-1))
    trace(TRACE_FATAL, "SS_WaitAndProcess(): Could not attach to shm [%s] (dcu)\n",strerror(errno));

  *ss_n_default_children_used = 0;

  default_child_pids = (pid_t*)shmat(shm_id_dcp, 0, 0);
  if (default_child_pids == (pid_t*)(-1))
    trace(TRACE_FATAL, "SS_WaitAndProcess(): Could not attach to shm [%s] (dcp)\n",strerror(errno));

  memset(default_child_pids, 0, default_children * SHM_ONE_DCP_ALLOC_SIZE);

  xtrachild_pids = (pid_t*)shmat(shm_id_xc, 0, 0);
  if (xtrachild_pids == (pid_t*)(-1))
    trace(TRACE_FATAL, "SS_WaitAndProcess(): Could not attach to shm [%s] (xcp)\n",strerror(errno));

  memset(xtrachild_pids, 0, max_children * SHM_ONE_XC_ALLOC_SIZE);


  /* fork into default number of children */
  for (i=0, ss_nchildren=0; i<default_children; i++)
    {
      if (!fork())
	{
	  default_child_pids[i] = getpid();
	  n_connects=0;
	  break;
	}
      else
	{
	  while (default_child_pids[i] == 0) 
	    {
	      trace(TRACE_INFO, "SS_WaitAndProcess(): waiting for child to catch up...\n");
	      sleep(1); /* wait until child has catched up */
	    }
	  ss_nchildren++;
	}
    }

  if (getpid() == ss_server_pid)
    {
      trace(TRACE_INFO,"SS_WaitAndProcess(): default children PID's:\n");
      for (i=0; i<default_children; i++)
	trace(TRACE_INFO,"   %d\n", default_child_pids[i]);
    }

  for ( ;; )
    {
      /* this infinite loop is needed for killed default-children:
       * they should re-enter at the following if-statement
       */

      /* now split program into client part and server part */
      if (getpid() != ss_server_pid)
	{
	  /* this is a client process, process until killed */

	  for (;;)
	    {
	      /* wait for connect */
	      len = sizeof(saClient);
	      csock = accept(sock, (struct sockaddr*)&saClient, &len);

	      if (csock == -1)
		continue;    /* accept failed, refuse connection & continue */

	      /* let other processes know we're busy */
	      (*ss_n_default_children_used)++;
	      n_connects++;

	      /* zero-init */
	      memset(&client, 0, sizeof(client));

	      /* make streams */
	      client.rx = fdopen(dup(csock), "r");

	      if (!client.rx)
		{
		  /* read-FILE opening failure */
		  close(csock); 
		  continue;
		}

	      client.tx = fdopen(csock, "w");
	      if (!client.tx)
		{
		  /* write-FILE opening failure */
		  fclose(client.rx);
		  close(csock); 
		  memset(&client, 0, sizeof(client));

		  continue;
		}

	      setlinebuf(client.tx);
	      setlinebuf(client.rx);
/*	      setvbuf(client.tx, txbuf, _IOFBF, TXBUFSIZE);
*/
	      client.timeout = server_timeout; /* remember timeout */

#if LOG_USERS > 0
	      trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) accepted\n",
		    csock, inet_ntoa(saClient.sin_addr));
#endif

	      if ((*Login)(&client) == SS_LOGIN_OK)
		{
		  client.loginStatus = SS_LOGIN_OK; /* xtra, should have been set by Login() */
		}
	      else
		{
		  /* login failure */
		  fclose(client.rx);
		  fclose(client.tx);

		  memset(&client, 0, sizeof(client));
	      
		  close(csock);
	  
#if LOG_USERS > 0
		  trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) "
			"login refused, connection closed\n",
			csock, inet_ntoa(saClient.sin_addr));
#endif
		  continue;
		}

	      /* remember client IP-address */
	      strncpy(client.ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
	      client.ip[SS_IPNUM_LEN - 1] = '\0';

	      trace(TRACE_INFO, "SS_WaitAndProcess(): child accept, dcu: %d\n",
		    *ss_n_default_children_used);

	      /* handle client */
	      (*ClientHandler)(&client); 
	      alarm(0);           /* remove any installed timeout-alarms */

#if LOG_USERS > 0
	      trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) logged out, "
		    "connection closed\n",
		    csock, client.ip);
#endif

	      if (client.tx && client.rx)
		{
		  fflush(client.tx);
		  fclose(client.tx);
		  shutdown(fileno(client.rx),SHUT_RDWR);
		  fclose(client.rx);
	      
		  memset(&client, 0, sizeof(client));
		}

	      (*ss_n_default_children_used)--;

	      trace(TRACE_INFO, "SS_WaitAndProcess(): child close, dcu: %d\n",
		    *ss_n_default_children_used);

	      if (n_connects >= max_connects)
		{
		  /* maximum number of connections, commit suicide */
		  trace(TRACE_DEBUG,"Maximum # of connections reached, committing suicide...\n");
		  for (i=0; i<default_children; i++)
		    if (default_child_pids[i] == getpid())
		      {
			sleep(1); /* allow father process to catch up */
			default_child_pids[i] = 0;
			exit(0);
		      }
		}

	    } /* main client loop */
	}
      else
	{
	  /* this is the server process */

	  for (;;)
	    {
	      /* check if default-child have died 
	       * (their entries have been set to zero in default_child_pids[]) 
	       */
	      for (i=0; i<default_children; i++)
		{
		  if (default_child_pids[i] == 0 || kill(default_child_pids[i], 0) == -1)
		    {
		      /* def-child has died, re-create */
		      if (!fork())
			{
			  default_child_pids[i] = getpid();
			  n_connects=0;
			  break;  
			  /* after this break the if (getpid() == ss_server_pid) 
			     will be re-executed */
			}
		      else
			{
			  /* add a counter so we will not loop forever */
			  j=0;
			  while (default_child_pids[i] == 0 && ++j<100) usleep(100);
			}
		    }
		}

	      if (getpid() != ss_server_pid)
		break; /* this is the newly created defchld */


	      /* wait for a connect then fork if the maximum number of children
	       * hasn't already been reached
	       */

	      while ((deadchildpid = waitpid (-1, NULL, WNOHANG | WUNTRACED)) > 0)
		{
		  trace(TRACE_DEBUG,"got %ld from waitpid\n",deadchildpid);
		  for (i=0; i<max_children; i++)
		    if (xtrachild_pids[i] == deadchildpid)
		      {
			xtrachild_pids[i] = 0;
			ss_nchildren--;
			break;
		      }
		      
		}

	      /* clean up list */
	      for (i=0; i<max_children; i++)
		if (xtrachild_pids[i] && 
		    waitpid(xtrachild_pids[i], NULL, WNOHANG | WUNTRACED) == -1 &&
		    errno == ECHILD)
		  {
		    xtrachild_pids[i] = 0;
		    ss_nchildren--;
		  }

	      if (*ss_n_default_children_used < default_children)
		{
		  sleep(1); /* dont hog cpu */
		  continue; 
		}

	      if (ss_nchildren >= max_children)
		{
		  sleep(1);
		  continue;
		}


	      /* wait for connect */
	      len = sizeof(saClient);
	      csock = accept(sock, (struct sockaddr*)&saClient, &len);

	      if (csock == -1)
		continue;    /* accept failed, refuse connection & continue */

	      if (fork())
		{
		  ss_nchildren++;
		  close(csock);
		  continue;
		}
	      else
		{
		  /* save pid */
		  for (i=0; i<max_children; i++)
		    if (!xtrachild_pids[i])
		      {
			xtrachild_pids[i] = getpid();
			break;
		      }

		  /* zero-init */
		  memset(&client, 0, sizeof(client));

		  /* make streams */
		  client.rx = fdopen(dup(csock), "r");

		  if (!client.rx)
		    {
		      /* read-FILE opening failure */
		      close(csock); 
		      exit(0);
		    }

		  client.tx = fdopen(csock, "w");
		  if (!client.tx)
		    {
		      /* write-FILE opening failure */
		      fclose(client.rx);
		      close(csock); 
		      exit(0);
		    }

		  setlinebuf(client.tx);
		  setlinebuf(client.rx);
/*		  setvbuf(client.tx, txbuf, _IOFBF, TXBUFSIZE);
*/
		  client.timeout = server_timeout; /* remember timeout */

#if LOG_USERS > 0
		  trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) accepted\n",
			csock, inet_ntoa(saClient.sin_addr));
#endif

		  if ((*Login)(&client) == SS_LOGIN_OK)
		    {
		      client.loginStatus = SS_LOGIN_OK; /* xtra, should have been set by Login() */
		    }
		  else
		    {
		      /* login failure */
		      fclose(client.rx);
		      fclose(client.tx);
		      close(csock);
	  
#if LOG_USERS > 0
		      trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) "
			    "login refused, connection closed\n",
			    csock, inet_ntoa(saClient.sin_addr));
#endif
		      exit(0);
		    }

		  /* remember client IP-address */
		  strncpy(client.ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
		  client.ip[SS_IPNUM_LEN - 1] = '\0';

		  /* handle client */
		  (*ClientHandler)(&client); 
		  alarm(0);                   /* remove any installed timeout-alarms */

#if LOG_USERS > 0
		  trace(TRACE_MESSAGE,"SS_WaitAndProcess(): client @ socket %d (IP: %s) logged out, "
			"connection closed\n",
			csock, client.ip);
#endif

		  if (client.tx && client.rx)
		    {
		      fflush(client.tx);
		      fclose(client.tx);
		      shutdown(fileno(client.rx),SHUT_RDWR);
		      fclose(client.rx);

		      memset(&client, 0, sizeof(client));
		    }

		  return 0; /* child must exit */
		}
	    }
	}
    }

  return 0; /* unreachable code */
}


/*
 * SS_CloseServer()
 *
 * Closes the server socket.
 */
void SS_CloseServer(int sock)
{
  close(sock);
}


/*
 * SS_sighandler()
 *
 * Handels SIGPIPE, SIGCHLD
 */
void SS_sighandler(int sig, siginfo_t *info, void *data)
{
  pid_t PID;
  int status;
  int i;

  if (sig == SIGUSR1)
    {
      /* we are being 'ping'-ed */
      trace(TRACE_DEBUG,"SS_sighandler(): ping received");
      return;
    }

  if (sig == SIGALRM)
    {
      /* timeout occurred, close client, terminate process */
      trace(TRACE_DEBUG, "SS_sighandler(): PID %d received alarm (time-out)\n", getpid());

      /* close streams */
      if (client.tx)
	{
	  fprintf(client.tx, ss_timeout_msg);
	  fflush(client.tx);
	  fclose(client.tx);
	}

      if (client.rx)
	{
	  shutdown(fileno(client.rx),SHUT_RDWR);
	  fclose(client.rx);

	}

      (*the_client_cleanup)(&client);
      memset(&client, 0, sizeof(client));

      /* reset the entry of this process if it is a default child (so it can be restored) */
      for (i=0; i<n_default_children && default_child_pids; i++)
	if (getpid() == default_child_pids[i])
	  {
	    default_child_pids[i] = 0;
	    (*ss_n_default_children_used)--;
	    break;
	  }

      exit(0);
    }

  if (sig != SIGCHLD)
    {
      /* reset the entry of this process if it is a default child (so it can be restored) */
      for (i=0; i<n_default_children && default_child_pids; i++)
	if (getpid() == default_child_pids[i])
	  {
	    default_child_pids[i] = 0;
	    (*ss_n_default_children_used)--;
	    break;
	  }

      /* close streams */
      if (client.tx)
	{
	  fflush(client.tx);
	  fclose(client.tx);
	}
      
      if (client.rx)
	{
	  shutdown(fileno(client.rx),SHUT_RDWR);
	  fclose(client.rx);
	}

      (*the_client_cleanup)(&client);
      memset(&client, 0, sizeof(client));
    }

  switch (sig)
    {
    case SIGCHLD:
      trace(TRACE_DEBUG, "SS_sighandler(): cleaning up zombies..");
      PID = waitpid(info->si_pid, &status, WNOHANG | WUNTRACED);

      /* reset the entry of this process if it is a default child (so it can be restored) 
       * This is added because of SIGKILL's
       */
      if (info->si_pid == 0)
	{
	  /* SIGKILL occured, 'ping' every child we have */
	  trace(TRACE_DEBUG,"signal_handler(): SIGKILL from [%u]", info->si_uid);
	    
	  for (i=0; i<n_default_children && default_child_pids; i++)
	    {
	      if (default_child_pids[i] <= 0)
		continue;

	      if (kill(default_child_pids[i], 0) == -1 && errno == ESRCH)
		{
		  /* this child no longer exists */
		  trace(TRACE_DEBUG, "signal_handler(): cleaning up PID %u", default_child_pids[i]);
		  default_child_pids[i] = 0;
		  (*ss_n_default_children_used)--;
		  break;
		}
	    }
	}
      trace (TRACE_DEBUG,"signal_handler(): sigCHLD, cleaned");
      return;

      break;

    case SIGPIPE: 
      trace(TRACE_FATAL,"Received SIGPIPE\n");
      break;

    case SIGINT: 
      trace(TRACE_FATAL,"Received SIGINT\n");
      break;

    case SIGQUIT: 
      trace(TRACE_FATAL,"Received SIGQUIT\n");
      break;

    case SIGILL: 
      trace(TRACE_FATAL,"Received SIGILL\n");
      break;

    case SIGBUS: 
      trace(TRACE_FATAL,"Received SIGBUS\n");
      break;

    case SIGFPE: 
      trace(TRACE_FATAL,"Received SIGFPE\n");
      break;

    case SIGSEGV: 
      trace(TRACE_FATAL,"Received SIGSEGV\n");
      break;

    case SIGTERM: 
      trace(TRACE_FATAL,"Received SIGTERM\n");
      break;

    case SIGSTOP: 
      trace(TRACE_FATAL,"Received SIGSTOP\n");
      break;

    default:
      trace(TRACE_FATAL,"Received signal %d\n",sig);
    }
}

 





