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

ClientInfo *client_being_processed;
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
  fd_set rxSet,wkSet;
  struct sockaddr_in saClient;
  struct timeval timeout;
  int maxDesc,n,csock,len,r;
  ClientInfo client[SS_MAX_CLIENTS];
  struct sigaction act;

  /* init & install signal handler for broken pipe */
  memset(&act, 0, sizeof(act));

  act.sa_handler = SS_sighandler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;

  client_being_processed = NULL;
  sigaction(SIGPIPE, &act, 0);

  /* init fd-sets */
  FD_ZERO(&rxSet);
  FD_SET(sock, &rxSet);
  maxDesc = sock + 1;

  /* init data */
  memset(client, 0, sizeof(client));

  /* start server loop */
  for (;;)
    {
      /* copy rx (read) to wk (work) */
      FD_ZERO(&wkSet);
      for (r=0; r<maxDesc; r++)
	{
	  if (FD_ISSET(r, &rxSet)) 
	    FD_SET(r, &wkSet);
	}

      /* set timeout */
      timeout.tv_sec = SS_TIMEOUT;
      timeout.tv_usec = 0;

      /* wait for something to happen */
      do
	{
	  /* check for interrupts on select */
	  n = select(maxDesc, &wkSet, NULL, NULL, &timeout);
	} while (n == -1 && errno == EINTR) ;

      if (n == -1)
	{
	  /* error occurred */
	  switch (errno)
	    {
	    case EBADF:
	      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_WaitAndProcess(): select error: "
		       "invalid file descriptor in set.");
	      break;

	    case EINVAL:
	      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_WaitAndProcess(): select error: "
		       "invalid number of file descriptors.");
	      break;

	    case ENOMEM:
	      snprintf(ss_error_msg,SS_ERROR_MSG_LEN,"SS_WaitAndProcess(): select error: "
		       "not enough memory for internal tables.");
	      break;
	    }

	  /* fatal error, close all connections and exit */
	  /* NOTE: server socket remains */
	  for (csock = 0; csock<maxDesc; csock++)
	    {
	      if (csock == sock)
		continue; /* server FD */

	      if (FD_ISSET(csock, &wkSet))
		{
		  FD_CLR(csock, &rxSet);
		  fclose(client[csock].rx);
		  fclose(client[csock].tx);
		  close(csock);
		}
	    }

	  return -1;
	}
      else if (n == 0)
	{
	  /* timeout occurred, ignore it */
	  continue;
	}
      
      /* check for connects */
      if (FD_ISSET(sock, &wkSet))
	{
	  /* wait for connect */
	  len = sizeof(saClient);
	  csock = accept(sock, (struct sockaddr*)&saClient, &len);

	  if (csock == -1)
	    {
	      /* accept failed, refuse connection & continue */
	      continue;
	    }
      
	  /* check if client-limit would be exceeded */
	  if (csock >= SS_MAX_CLIENTS)
	    {
	      close(csock);
	      continue;
	    }

	  /* zero-init */
	  memset(&client[csock], 0, sizeof(ClientInfo));
	  client[csock].loginStatus = SS_LOGIN_FAIL;

	  /* make streams */
	  client[csock].fd = csock;
	  client[csock].rx = fdopen(csock, "r");
	  if (!client[csock].rx)
	    {
	      /* read-FILE opening failure */
	      close(csock); 
	      continue;
	    }

	  client[csock].tx = fdopen(dup(csock), "w");
	  if (!client[csock].tx)
	    {
	      /* write-FILE opening failure */
	      fclose(client[csock].rx);
	      close(csock); 
	      continue;
	    }

	  if (csock+1 > maxDesc) 
	    maxDesc = csock+1;

#if LOG_USERS > 0
	  trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) accepted\n",
		  csock, inet_ntoa(saClient.sin_addr));
#endif

	  if ((*Login)(&client[csock]) == SS_LOGIN_OK)
	    {
	      FD_SET(csock, &rxSet); 	  /* OK */
	      client[csock].loginStatus = SS_LOGIN_OK; /* xtra, should have been set by Login() */
	    }
	  else
	    {
	      /* login failure */
	      fclose(client[csock].rx);
	      fclose(client[csock].tx);
	      close(csock);

#if LOG_USERS > 0
	  trace(TRACE_MESSAGE,"** Server: client @ socket %d (IP: %s) login refused, connection closed\n",
		  csock, inet_ntoa(saClient.sin_addr));
#endif

	      continue;
	    }

	  /* remember client IP-address */
	  strncpy(client[csock].ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
	  client[csock].ip[SS_IPNUM_LEN - 1] = '\0';
	}

      /* check for some work to do (client requests) */
      for (csock = 0; csock<maxDesc; csock++)
	{
	  if (csock == sock)
	    continue; /* server FD */

	  if (FD_ISSET(csock, &wkSet))
	    {
	      client_being_processed = &client[csock];

	      if ((*ClientHandler)(&client[csock]) == EOF)
		{

#if LOG_USERS > 0
	  fprintf(stderr,"** Server: client @ socket %d (IP: %s) logged out, connection closed\n",
		  csock, client[csock].ip);
#endif

		  FD_CLR(csock, &rxSet);
		  if (client[csock].id)
		    free(client[csock].id);
		}

	      client_being_processed = NULL;
	    }
	}

      /* try to lower maxDesc */
      for (csock=maxDesc-1; csock>=0 && !FD_ISSET(csock,&rxSet); csock=maxDesc-1)
	maxDesc = csock;
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
 * Handels SIGPIPE 
 */
void SS_sighandler(int sig)
{
  /* fprintf(stderr," !!! SIGPIPE !!! \n");*/ /* UNSAFE FUNCTION CALL !!! */

  if (client_being_processed)
    {
      /* close client file-descriptor */
      /*fprintf(stderr, "    !!! CLOSING FILE !!! \n");*/ /* UNSAFE FUNCTION CALL !!! */
      close(client_being_processed->fd);
    }
}

 
