/* $Id$
 * serverservice.c
 *
 * implementatie v/d server functies
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
  
  /* maak een tcp/ip socket */
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    {
      /* fout */
      sprintf(ss_error_msg,"%.*s",SS_ERROR_MSG_LEN,"SS_MakeServerSock(): kan socket niet aanmaken");
      return -1;
    }


  /* maak een (socket)adres */
  memset(&saServer, 0, sizeof(saServer));

  saServer.sin_family = AF_INET;
  saServer.sin_port = htons(atoi(port));

  r = inet_aton(ipaddr, &saServer.sin_addr);
  if (!r)
    {
      /* fout */
      sprintf(ss_error_msg,"%.*s",SS_ERROR_MSG_LEN,"SS_MakeServerSock(): "
	      "verkeerd ip-adres gespecificeerd");
      close(sock);
      return -1;
    }


  /* zet socket option: reuse address */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));


  /* bind het adres */
  len = sizeof(saServer);
  r = bind(sock, (struct sockaddr*)&saServer, len);

  if (r == -1)
    {
      /* fout */
      sprintf(ss_error_msg,"%.*s",SS_ERROR_MSG_LEN,"SS_MakeServerSock(): "
	      "kan adres niet aan socket binden");
      close(sock);
      return -1;
    }


  r = listen(sock, SS_BACKLOG);
  if (r == -1)
    {
      /* fout */
      sprintf(ss_error_msg,"%.*s",SS_ERROR_MSG_LEN,"SS_MakeServerSock(): kan socket niet laten luisteren");
      close(sock);
      return -1;
    }


  /* serversocket is klaar, kijk of de signals afgevangen moeten worden */
  if (sighandmode == SS_CATCH_KILL)
    {
    }

  sprintf(ss_error_msg,"%.*s",SS_ERROR_MSG_LEN,"Alles ok");
  return sock;
}


/*
 * SS_WaitAndProcess()
 * 
 * Wacht op clients, laat ze inloggen en roept een handler aan voor
 * requests.
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

  /* init & install signal handler voor broken pipe */
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

  /* begin de server loop */
  for (;;)
    {
      /* copieer rx (read) naar wk (work) */
      FD_ZERO(&wkSet);
      for (r=0; r<maxDesc; r++)
	{
	  if (FD_ISSET(r, &rxSet)) 
	    FD_SET(r, &wkSet);
	}

      /* zet de timeout */
      timeout.tv_sec = SS_TIMEOUT;
      timeout.tv_usec = 0;

      /* wacht op een happening */

      do
	{
	  /* controleer op interrupts voor de select */
	  n = select(maxDesc, &wkSet, NULL, NULL, &timeout);
	} while (n == -1 && errno == EINTR) ;

      if (n == -1)
	{
	  /* fout opgetreden */
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

	  /* gooi alle clients dicht */
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
	  /* timeout */
	  continue;
	}
      
      /* check voor een connectie */
      if (FD_ISSET(sock, &wkSet))
	{
	  /* wacht op de connectie */
	  len = sizeof(saClient);
	  csock = accept(sock, (struct sockaddr*)&saClient, &len);

	  if (csock == -1)
	    {
	      /* fout bij accept */
	      continue;
	    }
      
	  /* kijk of deze er nog bij kan */
	  if (csock >= SS_MAX_CLIENTS)
	    {
	      close(csock);
	      continue;
	    }

	  /* zero-init */
	  memset(&client[csock], 0, sizeof(ClientInfo));
	  client[csock].loginStatus = SS_LOGIN_FAIL;

	  /* maak de streams */
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
	  fprintf(stderr,"** Server: client @ socket %d (IP: %s) accepted\n",
		  csock, inet_ntoa(saClient.sin_addr));
#endif

	  if ((*Login)(&client[csock]) == SS_LOGIN_OK)
	    {
	      FD_SET(csock, &rxSet); 	  /* OK */
	      client[csock].loginStatus = SS_LOGIN_OK; /* dubbel, hoort gezet te worden door Login() */
	    }
	  else
	    {
	      /* login failure */
	      fclose(client[csock].rx);
	      fclose(client[csock].tx);
	      close(csock);

#if LOG_USERS > 0
	  fprintf(stderr,"** Server: client @ socket %d (IP: %s) login refused, connection closed\n",
		  csock, inet_ntoa(saClient.sin_addr));
#endif

	      continue;
	    }

	  /* sla het IP-adres v/d client op */
	  strncpy(client[csock].ip, inet_ntoa(saClient.sin_addr), SS_IPNUM_LEN);
	  client[csock].ip[SS_IPNUM_LEN - 1] = '\0';
	}

      /* kijk of er wat te doen is voor een bepaalde client */
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

      /* probeer maxDesc naar beneden te halen */
      for (csock=maxDesc-1; csock>=0 && !FD_ISSET(csock,&rxSet); csock=maxDesc-1)
	maxDesc = csock;
    }

  return 0; /* unreachable code */
}


/*
 * SS_CloseServer()
 *
 * Gooit de boel dicht.
 */
void SS_CloseServer(int sock)
{
  close(sock);
}


/*
 * SS_sighandler()
 *
 * Handelt een SIGPIPE af
 */
void SS_sighandler(int sig)
{
  /*fprintf(stderr," !!! SIGPIPE !!! \n");*/ /* UNSAFE FUNCTION CALL !!! */

  if (client_being_processed)
    {
      /* gooi de file-descriptor v/d client dicht */
      /*fprintf(stderr, "    !!! CLOSING FILE !!! \n");*/ /* UNSAFE FUNCTION CALL !!! */
      close(client_being_processed->fd);
    }
}

 
