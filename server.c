/* 
 * server.c
 *
 * code to implement a network server
 */

#include "debug.h"
#include "server.h"
#include "serverchild.h"
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


int GeneralStopRequested = 0;
int Restart = 0;
pid_t ParentPID = 0;

/* some extra prototypes (defintions are below) */
void ParentSigHandler(int sig, siginfo_t *info, void *data);


int SetParentSigHandler()
{
  struct sigaction act;

  /* init & install signal handlers */
  memset(&act, 0, sizeof(act));

  act.sa_sigaction = ParentSigHandler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_SIGINFO;

  sigaction(SIGCHLD, &act, 0);
  sigaction(SIGINT, &act, 0);
  sigaction(SIGQUIT, &act, 0);
  sigaction(SIGILL, &act, 0);
  sigaction(SIGBUS, &act, 0);
  sigaction(SIGFPE, &act, 0);
  sigaction(SIGSEGV, &act, 0);
  sigaction(SIGTERM, &act, 0);
  sigaction(SIGALRM, &act, 0);
  sigaction(SIGHUP, &act, 0);

  return 0;
}


int StartServer(serverConfig_t *conf)
{
  int i,stillSomeAlive,cnt;
  pid_t *pid = (pid_t*)malloc(sizeof(pid_t) * conf->nChildren);
  ChildInfo_t childinfo;

  if (!pid)
    trace(TRACE_FATAL, "StartServer(): no memory for PID list. Fatal.");

  if (!conf)
    trace(TRACE_FATAL, "StartServer(): NULL configuration");

  trace(TRACE_DEBUG, "StartServer(): init");

  ParentPID = getpid();
  Restart = 0;
  GeneralStopRequested = 0;
  SetParentSigHandler();

//  AllocSharedMemory();
//  AttachSharedMemory();

  trace(TRACE_DEBUG, "StartServer(): init ok. Creating children..");

  childinfo.maxConnect = conf->childMaxConnect;
  childinfo.listenSocket = conf->listenSocket;
  childinfo.timeout = conf->timeout;
  childinfo.ClientHandler = conf->ClientHandler;
  childinfo.timeoutMsg = conf->timeoutMsg;
  childinfo.resolveIP = conf->resolveIP;


  for (i=0; i<conf->nChildren; i++)
    {
      if ( (pid[i] = CreateChild(&childinfo)) == -1)
	{
	  trace(TRACE_ERROR, "StartServer(): could not create child");
	  trace(TRACE_ERROR, "StartServer(): killing children");
	  
	  while (--i >= 0)
	    kill(pid[i], SIGKILL);

	  trace(TRACE_FATAL, "StartServer(): could not create children. Fatal.");
	}
    }

  trace(TRACE_DEBUG, "StartServer(): children created, starting main service loop");
  
  while (!GeneralStopRequested)
    {
      for (i=0; i<conf->nChildren; i++)
	{
	  if (waitpid(pid[i], NULL, WNOHANG|WUNTRACED) == pid[i])
	    {
	      trace(TRACE_DEBUG, "StartServer(): child [%u] has exited", (unsigned)pid[i]);
	      trace(TRACE_DEBUG, "StartServer(): creating new child");
	      pid[i] = CreateChild(&childinfo);
	    }
	}

      sleep(1);
    }

  trace(TRACE_INFO, "StartServer(): General stop requested. Killing children.. ");

  stillSomeAlive = 1;
  cnt = 0;
  while (stillSomeAlive && cnt < 10)
    {
      stillSomeAlive = 0;
      cnt++;

      for (i=0; i<conf->nChildren; i++)
	{
	  if (pid[i] == 0)
	    continue;

	  if (CheckChildAlive(pid[i]))
	    {
	      trace(TRACE_DEBUG, "StartServer(): child [%d] is still alive, sending SIGTERM", pid[i]);
	      kill(pid[i], SIGTERM);
	      usleep(1000);
	    }
	  else
	    trace(TRACE_DEBUG, "StartServer(): child [%d] is dead, zombie not yet cleaned", pid[i]);

	      
	  if (waitpid(pid[i], NULL, WNOHANG|WUNTRACED) == pid[i]) 
	    {
	      trace(TRACE_DEBUG, "StartServer(): child [%d] has exited, zombie cleaned up", pid[i]);
	      pid[i] = 0;
	    }
	  else
	    {
	      stillSomeAlive = 1;
	      trace(TRACE_DEBUG, "StartServer(): child [%d] hasn't provided exit status yet", pid[i]);
	    }
	}
      
      if (stillSomeAlive)
	usleep(500);
    }
  
  if (stillSomeAlive)
    {
      trace(TRACE_INFO, "StartServer(): not all children terminated at SIGTERM, killing hard now");

      for (i=0; i<conf->nChildren; i++)
	{
	  if (pid[i] != 0)
	    kill(pid[i], SIGKILL);;
	}
    }


//  DeleteSharedMemory();

  free(pid);
  return Restart;
}
      

void ParentSigHandler(int sig, siginfo_t *info, void *data)
{
  if (ParentPID != getpid())
    {
      trace(TRACE_INFO, "ParentSigHandler(): i'm no longer father");
      ChildSigHandler(sig, info, data); /* this call is for a child but it's handler is not yet installed */
    }

#ifdef _USE_STR_SIGNAL
  trace(TRACE_INFO, "ParentSigHandler(): got signal [%s]", strsignal(sig));
#else
  trace(TRACE_INFO, "ParentSigHandler(): got signal [%d]", sig);
#endif

  switch (sig)
    {
    case SIGCHLD:
      break; /* ignore, wait for child in main loop */

    case SIGHUP:
      trace(TRACE_DEBUG, "ParentSigHandler(): SIGHUP, setting Restart");
      Restart = 1;

    default:
      GeneralStopRequested = 1;
    }
}


int CreateSocket(serverConfig_t *conf)
{
  int sock, r, len;
  struct sockaddr_in saServer;
  int so_reuseaddress;
  
  /* make a tcp/ip socket */
  sock = socket(PF_INET, SOCK_STREAM, 0);
  if (sock == -1)
    trace(TRACE_FATAL, "CreateSocket(): socket creation failed [%s]", strerror(errno));

  trace(TRACE_DEBUG, "CreateSocket(): socket created");

  /* make an (socket)address */
  memset(&saServer, 0, sizeof(saServer));

  saServer.sin_family = AF_INET;
  saServer.sin_port = htons(conf->port);

  if (conf->ip[0] == '*')
    saServer.sin_addr.s_addr = htonl(INADDR_ANY); 
  else
    {
      r = inet_aton(conf->ip, &saServer.sin_addr);
      if (!r)
	{
	  close(sock);
	  trace(TRACE_FATAL, "CreateSocket(): invalid IP [%s]", conf->ip);
	}
    }

  trace(TRACE_DEBUG, "CreateSocket(): socket IP requested [%s] OK", conf->ip);

  /* set socket option: reuse address */
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

  /* bind the address */
  len = sizeof(saServer);
  r = bind(sock, (struct sockaddr*)&saServer, len);

  if (r == -1)
    {
      close(sock);
      trace(TRACE_FATAL, "CreateSocket(): could not bind address to socket");
    }

  trace(TRACE_DEBUG, "CreateSocket(): IP bound to socket");

  r = listen(sock, BACKLOG);
  if (r == -1)
    {
      close(sock);
      trace(TRACE_FATAL, "CreateSocket(): error making socket listen [%s]", strerror(errno));
    }
  
  trace(TRACE_INFO, "CreateSocket(): socket creation complete");
  conf->listenSocket = sock;

  return 0;
}
  


