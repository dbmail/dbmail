/* $Id$
 * serverservice.c
 *
 * implements server functionality
 *
 * (c)2001 IC&S
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include "serverservice.h"
#include "debug.h"

#define LOG_USERS 0

#define SS_ERROR_MSG_LEN 100
char ss_error_msg[SS_ERROR_MSG_LEN];

char *SS_GetErrorMsg()
{
  return ss_error_msg;
}

void SS_sighandler(int sig);

int SS_MakeServerSock(const char *ipaddr, const char *port, int sighandmode)
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

  r = inet_aton(ipaddr, &saServer.sin_addr);
  if (!r)
    {
      /* error */
      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_MakeServerSock(): "
	      "wrong ip-address specified");
      close(sock);
      return -1;
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


  /* serversocket is done, check for signal-handling */ 
  if (sighandmode == SS_CATCH_KILL)
    {
      /* !!! */
    }

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
int SS_WaitAndProcess(int sock, int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*))
{
  struct sockaddr_in saClient;
  int csock,len;
  ClientInfo client;
  struct sigaction act;
  pid_t PID;

  /* init & install signal handler for SIGCHLD */
  memset(&act, 0, sizeof(act));

  act.sa_handler = SS_sighandler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  sigaction(SIGCHLD, &act, 0);
  sigaction(SIGPIPE, &act, 0);

  /* start server loop */
  for (;;)
    {
      /* wait for connect */
      len = sizeof(saClient);
      csock = accept(sock, (struct sockaddr*)&saClient, &len);

      if (csock == -1)
	{
	  /* accept failed, refuse connection & continue */
	  continue;
	}
      
      /* fork into server/client processes */
      if ( (PID = fork()) == -1)
	{
	  /* fork failed */
	  close(csock);
	  continue;
	}

      if (PID > 0)
	{
	  /* parent process */
/*	  close(csock); */
	  continue;
	}

      /* 
       * now entering CHILD proces 
       */

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
	  continue;
	}

      setlinebuf(client.rx);
      setlinebuf(client.tx);

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

	  continue;
	}

      /* remember client IP-address */
      strncpy(client.ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
      client.ip[SS_IPNUM_LEN - 1] = '\0';

      (*ClientHandler)(&client);

#if LOG_USERS > 0
      fprintf(stderr,"** Server: client @ socket %d (IP: %s) logged out, connection closed\n",
	      csock, client.ip);
#endif

      fflush(client.tx);
      fclose(client.tx);
      shutdown(fileno(client.rx),SHUT_RDWR);
      fclose(client.rx);

      exit(0); /* child process must exit */
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
void SS_sighandler(int sig)
{
  pid_t PID;
  int status;
  struct sigaction act;

  if (sig == SIGCHLD)
    {
      do 
	{
	  PID = waitpid(-1, &status, WNOHANG);
	} while (PID != -1);

      /* init & install signal handler for SIGCHLD */
      memset(&act, 0, sizeof(act));

      act.sa_handler = SS_sighandler;
      sigemptyset(&act.sa_mask);
      act.sa_flags = 0;

      sigaction(SIGCHLD, &act, 0);
    }
  else if (sig == SIGPIPE)
    {
      
    }

}

 
