/* $Id$
 * pop3 daemon */

#include "pop3.h"
#include "dbmysql.h"

#define INCOMING_BUFFER_SIZE 512
#define MAXTIMEOUT 300
#define IP_ADDR_MAXSIZE 16

#ifndef SHUT_RDWR
#define SHUR_RDWR 3
#endif

/* syslog */
#define PNAME "dbmail/pop3"

int state;
char *username=NULL, *password=NULL;
struct session curr_session;
char *myhostname;

char *buffer;
int done=1;

int error_count;


/* signal handler */
static void sigchld_handler (int signo)
	{
	pid_t PID;
	int status;

	if (signo == SIGCHLD)
		{
		while (waitpid (-1, &status, WNOHANG));
		return;
		}
	else
		trace (TRACE_STOP,"sigchld_handler(): received fatal signal [%d]",signo);
	}

int main (int argc, char *argv[])
{
	struct sockaddr_in adr_srvr;
	struct sockaddr_in adr_clnt;
	char *myhostname;
	char *theiraddress;

	struct hostent *clientinfo;

	int len_inet;
	int s = -1;
	int c = -1;
	int z;
	int children=0;		/* child process counter */
	FILE *tx = NULL;	/* write socket */
	FILE *rx = NULL;	/* read socket */

	/* open logs */

	openlog(PNAME, LOG_PID, LOG_MAIL);
	

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
	memtst((clientinfo=(struct hostent *)malloc(1))==NULL);
	
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

	
	adr_srvr.sin_family = AF_INET; 
	adr_srvr.sin_addr.s_addr = htonl (INADDR_ANY); /* bind to any address */
	adr_srvr.sin_port = htons (PORT); /* listning port */
	
	len_inet=sizeof (adr_srvr);

	s = socket (PF_INET, SOCK_STREAM, 0); /* creating the socket */
	if (s == -1 ) 
		trace (TRACE_FATAL,"main(): call socket(2) failed");

	z = bind (s, (struct sockaddr *)&adr_srvr, len_inet); /* bind to socket */
	if (z == -1 )
		trace (TRACE_FATAL,"main(): call bind(2) failed");

	z = listen (s, BACKLOG); /* make the socket listen */
	if (z == -1 )
		trace (TRACE_FATAL,"main(): call listen(2) failed");
	
	/* server loop */

	trace (TRACE_MESSAGE,"main(): DBmail pop3 server ready");
	
	for (;;)
		{
		if (children < MAXCHILDREN) 
			{
			if (!fork())
				{
				/* wait for a connection */
				len_inet = sizeof (adr_clnt);
				c = accept (s, (struct sockaddr *)&adr_clnt,
					&len_inet); /* incoming connection */
				
				if (c == -1)
					trace (TRACE_FATAL,"main(): call accept(2) failed");

				/* register incoming connection */
				theiraddress=inet_ntoa(adr_clnt.sin_addr);
				clientinfo=gethostbyname(theiraddress);
				
				if (theiraddress != NULL)
					trace (TRACE_MESSAGE,"main(): incoming connection from [%s] [resolved to: %s]"
							,theiraddress,clientinfo->h_name);
				else
					trace (TRACE_FATAL,"main(): fatal, could not get address of client"); 
				
				rx = fdopen (dup (c), "r"); /* duplicate descriptor and open it */
				if (!rx)
					{
					/* opening of descriptor failed */
					close(c);
					continue;
					}
	
				tx = fdopen (dup (c), "w"); /* same thing for writing */
					if (!tx)
					{
					/* opening of descriptor failed */
					close (c);
					continue;
					}
		
				/* set stream to line buffered mode 
				 * this way when we send a newline the buffer is flushed */
				setlinebuf(rx);
				setlinebuf(tx);
	
				/* process client requests */
		
				/* connect to the database */
				if (db_connect()< 0) 
					trace(TRACE_FATAL,"main(): could not connect to database");

				/* first initiate AUTHORIZATION state */
				state = AUTHORIZATION;
		
				memtst((buffer=(char *)malloc(INCOMING_BUFFER_SIZE))==NULL);
	
				/* sending greeting */
				fprintf (tx,"+OK DBMAIL (currently running at %s) welcome you\r\n",myhostname);
		
				/* no errors yet */
				error_count = 0;
				
				/* scanning for commands */
	
				while ((done>0) && (buffer=fgets(buffer,INCOMING_BUFFER_SIZE,rx)))
					done=pop3(tx,buffer); 

				/* we've reached the update state */
				state = UPDATE;

				/* memory cleanup */
				free(buffer);
	
				trace(TRACE_MESSAGE,"pop3(): user %s logging out [message=%lu, octets=%lu]",
					username, curr_session.virtual_totalmessages,
					curr_session.virtual_totalsize);

				if (done==0) 
					db_update_pop(&curr_session);
				else db_disconnect(); 
	
				fclose(tx);
				shutdown (fileno(rx), SHUT_RDWR);
				fclose(rx);
		
				/* we must send an exit as child */

				free(myhostname);
				exit(0);
				}
			else
				{
					children++;
				}
			}
		else
			{
			wait (NULL);
			children--;
			}
		}
	return 0;
}	
