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

#define LOG_USERS 0

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


/* list of PID's of the default-children */
pid_t *default_child_pids;
int n_default_children;

/* the client being processed, this data is global 
 * to be able to see it in the sighandler
 */
ClientInfo client;
int *ss_n_default_children_used;

/* transmit buffer */
#define TXBUFSIZE 1024
char txbuf[TXBUFSIZE];



char *SS_GetErrorMsg()
{
  return ss_error_msg;
}

void SS_sighandler(int sig, siginfo_t *info, void *data);


int SS_MakeServerSock(const char *ipaddr, const char *port, int default_children)
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

  n_default_children = default_children;

  snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"Server socket has been created.");
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
		      int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*))
{
  struct sockaddr_in saClient;
  int csock,len,i;
  struct sigaction act;
  int ss_nchildren=0;
  pid_t ss_server_pid=0;

  if (n_default_children != default_children)
    trace(TRACE_FATAL,"SS_WaitAndProcess(): incompatible number of default-children specified.\n");

  /* init & install signal handlers */
  memset(&act, 0, sizeof(act));

  act.sa_sigaction = SS_sighandler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_SIGINFO;

  sigaction(SIGCHLD, &act, 0);
  sigaction(SIGPIPE, &act, 0);
  sigaction(SIGINT, &act, 0);
  sigaction(SIGQUIT, &act, 0);
  sigaction(SIGILL, &act, 0);
  sigaction(SIGBUS, &act, 0);
  sigaction(SIGFPE, &act, 0);
  sigaction(SIGSEGV, &act, 0);
  sigaction(SIGTERM, &act, 0);
  sigaction(SIGSTOP, &act, 0);

  /* daemonize */
  if (daemonize)
    {
      if (fork())
	exit(0);
      setsid();

      if (fork())
	exit(0);
    }

  /* this process will be the server, remember pid */
  ss_server_pid = getpid();

  /* attach to shm */
  ss_n_default_children_used = (int*)shmat(shm_id_dcu, 0, 0);
  if (ss_n_default_children_used == (int*)(-1))
    trace(TRACE_FATAL, "SS_WaitAndProcess(): Could not attach to shm [%s]\n",strerror(errno));

  *ss_n_default_children_used = 0;

  default_child_pids = (pid_t*)shmat(shm_id_dcp, 0, 0);
  if (default_child_pids == (pid_t*)(-1))
    trace(TRACE_FATAL, "SS_WaitAndProcess(): Could not attach to shm [%s]\n",strerror(errno));

  memset(default_child_pids, 0, default_children * SHM_ONE_DCP_ALLOC_SIZE);


  /* fork into default number of children */
  for (i=0; i<default_children; i++)
    {
      if (!fork())
	{
	  default_child_pids[i] = getpid();
	  break;
	}
      else
	ss_nchildren++;
    }

  if (getpid() == ss_server_pid)
    {
      trace(TRACE_DEBUG,"SS_WaitAndProcess(): default children PID's:\n");
      for (i=0; i<default_children; i++)
	trace(TRACE_DEBUG,"   %d\n", default_child_pids[i]);
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

	      /* zero-init */
	      memset(&client, 0, sizeof(client));

	      /* make streams */
	      client.fd = csock;
	      client.rx = fdopen(csock, "r");

	      if (!client.rx)
		{
		  /* read-FILE opening failure */
		  close(csock); 
		  continue;
		}

	      client.tx = fdopen(dup(csock), "w");
	      if (!client.tx)
		{
		  /* write-FILE opening failure */
		  fclose(client.rx);
		  close(csock); 
		  memset(&client, 0, sizeof(client));

		  continue;
		}

	      setlinebuf(client.rx);
	      /*	  setlinebuf(client.tx);
	       */
	      setvbuf(client.tx, txbuf, _IONBF, 1);

	  
#if LOG_USERS > 0
	      trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) accepted\n",
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
		  trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) login refused, "
			"connection closed\n",
			csock, inet_ntoa(saClient.sin_addr));
#endif
		  continue;
		}

	      /* remember client IP-address */
	      strncpy(client.ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
	      client.ip[SS_IPNUM_LEN - 1] = '\0';

	  /* remember */
	      (*ss_n_default_children_used)++;
	      trace(TRACE_DEBUG, "[%ld] child accept, dcu: %d\n",getpid(),*ss_n_default_children_used);

	  /* handle client */
	      (*ClientHandler)(&client); 

#if LOG_USERS > 0
	      fprintf(stderr,"** Server: client @ socket %d (IP: %s) logged out, connection closed\n",
		      csock, client.ip);
#endif

	      if (client.tx && client.rx)
		{
		  fflush(client.tx);
		  fclose(client.tx);
		  shutdown(fileno(client.rx),SHUT_RDWR);
		  fclose(client.rx);
	      
		  memset(&client, 0, sizeof(client));

		  (*ss_n_default_children_used)--;
		}

	      trace(TRACE_DEBUG, "[%ld] child close, dcu: %d\n",getpid(),*ss_n_default_children_used);

	    } /* main client loop */
	}
      else
	{
	  /* this is the server process */

	  for (;;)
	    {
	      /* check if a default-child has died 
	       * (it's entry has been set to zero in default_child_pids[]) 
	       */
	      for (i=0; i<default_children && default_child_pids[i]; i++) ;
	      
	      if (i<default_children)
		{
		  /* def-child has died, re-create */
		  if (!fork())
		    break;  /* after this break the if (getpid() == ss_server_pid) will be re-executed */
		}
		  
	      /* wait for a connect then fork if the maximum number of children
	       * hasn't already been reached
	       */

	      if (*ss_n_default_children_used < default_children)
		{
		  sleep(1); /* dont hog cpu */
		  continue; 
		}

	      while (ss_nchildren >= max_children)
		{
		  /* maximum number of children has already been reached, wait for an exit */
		  wait(NULL);
		  ss_nchildren--;
		}

	      /* wait for connect */
	      len = sizeof(saClient);
	      csock = accept(sock, (struct sockaddr*)&saClient, &len);

	      if (csock == -1)
		continue;    /* accept failed, refuse connection & continue */

	      if (fork())
		{
		  ss_nchildren++;
		  continue;
		}
	      else
		{
		  /* zero-init */
		  memset(&client, 0, sizeof(client));

	      /* make streams */
		  client.fd = csock;
		  client.rx = fdopen(csock, "r");

		  if (!client.rx)
		    {
		      /* read-FILE opening failure */
		      close(csock); 
		      exit(0);
		    }

		  client.tx = fdopen(dup(csock), "w");
		  if (!client.tx)
		    {
		      /* write-FILE opening failure */
		      fclose(client.rx);
		      close(csock); 
		      exit(0);
		    }

		  setlinebuf(client.rx);
		  /*	      setlinebuf(client.tx);
		   */

		  setvbuf(client.tx, txbuf, _IONBF, 1);

#if LOG_USERS > 0
		  trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) accepted\n",
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
		      trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) login refused, "
			    "connection closed\n",
			    csock, inet_ntoa(saClient.sin_addr));
#endif
		      exit(0);
		    }

		  /* remember client IP-address */
		  strncpy(client.ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
		  client.ip[SS_IPNUM_LEN - 1] = '\0';

	      /* handle client */
		  (*ClientHandler)(&client); 

#if LOG_USERS > 0
		  fprintf(stderr,"** Server: client @ socket %d (IP: %s) logged out, connection closed\n",
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

  /* reset the entry of this process if it is a default child (so it can be restored) */
  for (i=0; i<n_default_children; i++)
    if (info->si_pid == default_child_pids[i])
      {
	default_child_pids[i] = 0;
	ss_n_default_children_used--;
      }

  switch (sig)
    {
    case SIGCHLD:
      do 
	{
	  PID = waitpid(info->si_pid, &status, WNOHANG);
	} while (PID != -1);

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

 





