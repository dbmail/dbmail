/*
 * injector.c
 *
 * Code for the SMTP injector program.
 */

#include "auth.h"
#include "db.h"
#include "debug.h"
#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_LINESIZE 1024
#define UID_SIZE 70
#define NO_SUCH_USER 1

/* syslog */
#define PNAME "dbmail/smtp-injector"

char default_scan_field[] = "deliver-to";
char blk[READ_BLOCK_SIZE + MAX_LINESIZE];
char header[READ_BLOCK_SIZE + MAX_LINESIZE];
unsigned hdrlen;

int add_address(const char *uname, 
		struct list *userids, struct list *bounces, struct list *fwds);
int add_username(const char *uname, struct list *userids);
int process_args(int argc, char *argv[], 
		 struct list *userids, struct list *bounces, struct list *fwds, 
		 char **field_to_scan);
unsigned process_header(struct list *userids, struct list *bounces, struct list *fwds, 
			const char *field, char *hdrdata, u64_t *newlines, char **bounce_path);

int send_bounces(struct list *bounces, u64_t len, const char *bouncepath, 
		 const char *sendmail, const char *postmaster, const char *fromaddr);

int send_quotum_bounce(const char *bouncepath, const char *sendmail, const char *postmaster, const char *fromaddr);



int main(int argc, char *argv[])
{
  char *field_to_scan = NULL;
  char *bouncepath=0;
  struct list userids, bounces, fwds;
  struct element *thisuser = NULL;
  char uniqueid[UID_SIZE];
  unsigned len,i;
  u64_t size,newlines=0;
  u64_t *qleft = NULL,*uids = NULL;
  FILE **pipes = NULL;
  char *sendmail = NULL, *postmaster = NULL, *fromaddr = NULL;
  int using_dbase = 0;
  char save, *sub = NULL;
  struct element *el;

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_DEBUG, 1, 0);  /* do not spill time on reading settings */

  list_init(&userids);
  list_init(&bounces);
  list_init(&fwds);

  /* check command-line options */
  if (argc < 2)
    {
      printf ("\nUsage: %s -n [headerfield]   for normal deliveries "
	      "(default: \"deliver-to\" header)\n",argv[0]);
      printf ("       %s -m \"mailbox\" -u [username] for delivery to mailbox (name)\n"
              ,argv[0]);
      printf ("       %s -d [addresses]  for delivery without using scanner\n",argv[0]);
      printf ("       %s -u [usernames]  for direct delivery to users\n\n",argv[0]);
      return 0;
    }

  /* open dbase connections */
  if (db_connect() != 0 || auth_connect() != 0)
    {
      trace(TRACE_FATAL,"main(): could not connect to dbases");
      return -1;
    }

  /* get sending utility */
  sendmail = db_get_config_item ("SENDMAIL", CONFIG_MANDATORY);
  postmaster = db_get_config_item ("POSTMASTER", CONFIG_MANDATORY);
  fromaddr = db_get_config_item ("DBMAIL_FROM_ADDRESS", CONFIG_MANDATORY);

  if (!sendmail || !postmaster || !fromaddr)
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): no SENDMAIL util");
      return -1;
    }

  if (process_args(argc, argv, &userids, &bounces, &fwds, &field_to_scan) != 0)
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): error processing command line options");
    }

  if ((len = process_header(&userids, &bounces, &fwds, field_to_scan, blk, &newlines, &bouncepath)) == -1)
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): error processing header");
      return -1;
    }

  if (userids.total_nodes > 0)
    using_dbase = 1;

  /* 
   * OK header read, user lists generated. 
   * First create a temporary unique-ID that will be the same for all inserted 
   * messages. Next place the messages in the table, the headers as first blocks
   */
     
  /* first allocate memory for pipes, quotum checks & user ids*/
  if (! (qleft = (u64_t*)my_malloc(sizeof(u64_t) * userids.total_nodes)) )
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): out of memory [line %d]", __LINE__);
      return -1;
    }

  if (! (uids = (u64_t*)my_malloc(sizeof(u64_t) * userids.total_nodes)) )
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      my_free(qleft);
      trace(TRACE_FATAL, "main(): out of memory [line %d]", __LINE__);
      return -1;
    }
  
  if (! (pipes = (FILE**)my_malloc(sizeof(FILE*) * fwds.total_nodes)) )
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      my_free(qleft);
      my_free(uids);
      trace(TRACE_FATAL, "main(): out of memory [line %d]", __LINE__);
      return -1;
    }
  
  /* open pipes (fwds) */
  el = list_getstart(&fwds);

  for (i=0; i<fwds.total_nodes; i++)
    {
      pipes[i] = popen(sendmail, "w");
      if (!pipes[i])
	{
	  db_disconnect();
	  auth_disconnect();
	  list_freelist(&userids.start);
	  list_freelist(&bounces.start);
	  list_freelist(&fwds.start);
	  my_free(qleft);
	  my_free(uids);
	  trace(TRACE_FATAL, "main() could not open pipe [%d]", i);
	  return -1;
	}

      /* write the header right now */
      sub = strstr(blk, "To: ");
      if (!sub)
	sub = strstr(blk, "to: ");

      if (!sub)
	{
	  fwrite(blk, 1, len, pipes[i]);
	  fprintf(pipes[i],"To: %s", (char*)el->data);
	}
      else
	{
	  blk[len] = 0;

	  /* remove this To: line */
	  save = sub[0];
	  sub[0] = 0;
	  fprintf(pipes[i], "%s", blk);
	  sub[0] = save;
	  sub = strstr(sub, "\n");
	  if (!sub || sub[0] == '\0') /* should never happen .. */
	    fprintf(pipes[i],"To: %s\n", (char*)el->data);
	  else
	    {
	      fprintf(pipes[i], "To: %s\n", (char*)el->data);
	      fprintf(pipes[i], "%s", sub); /* sub is terminated by blk[]'s  termination */
	    }
	}
      
      el = el->nextnode;
    }

  /* process bounces */
  if (send_bounces(&bounces, len, bouncepath, sendmail, postmaster, fromaddr) != 0)
    {
      trace(TRACE_FATAL,"main(): error bouncing messages");
      return -1;
    }

  srand(time(NULL));
  snprintf(uniqueid, UID_SIZE, "%lu%u%u", time(NULL), getpid(), rand()%1000);
  
  if (using_dbase)
    {
      thisuser = userids.start;
      i = 0;
      while (thisuser)
	{
	  db_insert_message(*(u64_t*)thisuser->data, 0, uniqueid);
	  qleft[i] = auth_getmaxmailsize(*(u64_t*)thisuser->data) - db_get_quotum_used(*(u64_t*)thisuser->data);
	  uids[i]  = *(u64_t*)thisuser->data;
	  i++;
	  thisuser = thisuser->nextnode;
	}

      /* insert the header */
      db_insert_message_block_multiple(uniqueid, blk, len);
    }


  /* swap to saev header */
  memmove(header, blk, len);
  hdrlen = len;
  header[hdrlen] = 0;


  /* read data & insert */
  size = 0;
  while (!feof(stdin) && !ferror(stdin))
    {
      len = fread(blk, sizeof(char), READ_BLOCK_SIZE-1, stdin);
      blk[len] = 0; /* terminate */
      
      if (using_dbase)
	{
	  if (db_insert_message_block_multiple(uniqueid, blk, len) != 0)
	    {
	      db_disconnect();
	      auth_disconnect();
	      list_freelist(&userids.start);
	      list_freelist(&bounces.start);
	      list_freelist(&fwds.start);
	      my_free(qleft);
	      my_free(pipes);
	      my_free(uids);
	      trace(TRACE_FATAL, "main(): error inserting message block");
	      return -1;
	    }
	}
	  
      /* do pipes */
      for (i=0; i<fwds.total_nodes; i++)
	fwrite(blk, 1, len, pipes[i]);

      for (i=0; using_dbase && i<userids.total_nodes; i++)
	{
	  if (qleft >= 0)
	    {
	      qleft[i] -= len;
	      if (qleft[i] < 0)
		{
		  /* not enough quotum left */
		  if (db_rollback_insert(uids[i], uniqueid) != 0)
		    trace(TRACE_ERROR, "main(): error deleting too large message unique-id "
			  "[%s], continuing...", uniqueid);
		  else
		    trace(TRACE_INFO,"main(): message not inserted for user [%llu] (quotum exceeded)",
			  uids[i]);

		  send_quotum_bounce(bouncepath, sendmail, postmaster, fromaddr);
		}
	    }
	}  
      
      size += len;
      newlines++;
    }

  if (using_dbase)
    {
      /* update messages */
      db_update_message_multiple(uniqueid, size, size+newlines);

      
    }

  /* cleanup */
  db_disconnect();
  auth_disconnect();
  list_freelist(&userids.start);
  list_freelist(&bounces.start);
  list_freelist(&fwds.start);
  my_free(qleft);
  my_free(pipes);
  my_free(uids);

  return 0;
}


/*
 * process_header()
 *
 * Reads in a mail header (from stdin, reading is done until '\n\n' is encountered).
 * If field != NULL scanning is done for delivery on that particular field.
 *
 * Data is saved in hdrdata which should be capable of holding at least READ_BLOCK_SIZE characters.
 *
 * returns data cnt on success, -1 on failure
 */
unsigned process_header(struct list *userids, struct list *bounces, struct list *fwds, 
			const char *field, char *hdrdata, u64_t *newlines, char **bounce_path)
{
  int len = field ? strlen(field) : 0;
  char *left, *right, *curr, save, *line;
  char *frompath = 0;
  unsigned cnt = 0;
  *newlines = 0;
  *bounce_path = 0;

  while (!feof(stdin) && !ferror(stdin) && cnt < READ_BLOCK_SIZE)
    {
      line = &hdrdata[cnt]; /* write directly to hdrdata */
      fgets(line, READ_BLOCK_SIZE - cnt, stdin);
      (*newlines)++;
      if (strcmp(line, "\n") == 0)
	break;

      cnt += strlen(line);

      if (field && strncasecmp(line, field, len) == 0 && line[len] == ':' && line[len+1] == ' ')
	{
	  /* found the field we're scanning for */
	  trace(TRACE_DEBUG, "process_header(): found field");
	      
	  curr = &line[len];

	  while (curr && *curr)
	    {
	      left = strchr(curr, '@');
	      if (!left)
		break;

	      right = left;

	      /* walk to the left */
	      while (left != line && left[0]!='<' && left[0]!=' ' && left[0]!='\0' && left[0]!=',')
		left--;
		  
	      left++; /* walked one back too far */
		  
	      /* walk to the right */
	      while (right[0]!='>' && right[0]!=' ' && right[0]!='\0' && right[0]!=',')
		right++;
		  
	      save = *right;
	      *right = 0; /* terminate string */
	      
	      if (add_address(left, userids, bounces, fwds) != 0)
		trace(TRACE_ERROR,"process_header(): could not add [%s]", left);

	      trace(TRACE_DEBUG,"process_header(): processed [%s]", left);
	      *right = save;
	      curr = right;
	    }
	}
      else if (field && !(*bounce_path) && strncasecmp(line, "return-path", strlen("return-path")) == 0)
	{
	  /* found return-path */
	  *bounce_path = (char*)my_malloc(strlen(line));
	  if (!(*bounce_path))
	    return -1;

	  left = strchr(line, ':');
	  if (left)
	    strcpy(*bounce_path, &left[1]);
	  else
	    {
	      my_free(bounce_path);
	      bounce_path = 0;
	    }
	}
      else if (field && !(*bounce_path) && !frompath && strncasecmp(line, "from", strlen("from")) == 0)
	{
	  /* found from field */
	  frompath = (char*)my_malloc(strlen(line));
	  if (!frompath)
	    return -1;

	  left = strchr(line, ':');
	  if (left)
	    strcpy(frompath, &left[1]);
	  else
	    {
	      my_free(frompath);
	      frompath = 0;
	    }
	}
    }
	
  if (frompath)
    {
      if (!(*bounce_path))
	*bounce_path = frompath;
      else
	my_free(frompath);
    }

  trace(TRACE_DEBUG,"process_header(): found bounce path [%s]", *bounce_path ? *bounce_path : "<<none>>");

  return cnt;
}



/*
 * process_args()
 *
 * processes the command line arguments for the smtp injector program.
 * 
 * returns 0 on success, -1 on failure.
 */
int process_args(int argc, char *argv[], 
		 struct list *userids, struct list *bounces, struct list *fwds,
		 char **field_to_scan)
{
  int i=0;

  *field_to_scan = NULL;

  if (strcmp(argv[1], "-n") == 0)
    {
      /* read deliver addresses from header */

      if (argc > 2)
	*field_to_scan = argv[2]; /* header field specified */
      else
	*field_to_scan = default_scan_field;
    }
  else if (strcmp(argv[1], "-d") == 0)
    {
      /* direct delivery, addresses are specified */
    
      if (add_address(argv[2], userids, bounces, fwds) != 0)
	{
	  trace(TRACE_ERROR,"process_args(): error adding address [%s]", argv[i]);
	  return -1;
	}
    }
  else if (strcmp(argv[1], "-u") == 0)
    {
      /* direct delivery, usernames are specified */

      for (i=2; argv[i]; i++)
	{
	  if (add_username(argv[i], userids) != 0)
	    {
	      trace(TRACE_ERROR,"process_args(): error adding user [%s]", argv[i]);
	      return -1;
	    }
	}
    }
  else if (strcmp(argv[1], "-m") == 0)
    {
      /* direct delivery, usernames+mailboxes are specified */
      
    }

  return 0;
}



/*
 * add_address()
 *
 * takes an e-mail address and finds the correct delivery for it:
 * internal (numeric id), bounce, forward
 *
 * returns 0 on success, -1 on failure
 */
int add_address(const char *address, struct list *userids, struct list *bounces, struct list *fwds)
{
  char *domain;

  if (auth_check_user_ext(address, userids, fwds, -1) == 0)
    {
      /* not in alias table
       * check for a domain fwd first; if none present
       * then make it a bounce
       */
      
      domain = strchr(address, '@');
      if (!domain)
	{
	  /* ?? no '@' in address ? */
	  trace(TRACE_ERROR, "add_address(): got invalid address [%s]", address);
	}
      else
	{
	  if (auth_check_user_ext(domain, userids, fwds, -1) == 0)
	    {
	      /* ok no domain fwds either --> bounce */
	      if (list_nodeadd(bounces, address, strlen(address)+1) == 0)
		{
		  trace(TRACE_ERROR, "add_address(): could not add bounce [%s]", address);
		  return -1;
		}
	    }
	}
    }

  return 0;
}
      

/*
 * add_username()
 *
 * adds the (numeric) ID of the user uname to the list of ids.
 *
 * returns 0 on success, -1 on failure
 */
int add_username(const char *uname, struct list *userids)
{
  u64_t uid;

  uid = auth_user_exists(uname);
  switch (uid)
    {
    case (u64_t)(-1):
      trace(TRACE_ERROR,"add_username(): error verifying user existence");
      break;
    case 0:
      trace(TRACE_INFO,"add_username(): non-existent user specified");
      break;
    default:
      trace(TRACE_DEBUG,"add_username(): adding user [%s] id [%llu] to list", uname, uid);
      if (list_nodeadd(userids, &uid, sizeof(uid)) == NULL)
	{
	  trace(TRACE_ERROR,"add_username(): out of memory");
	  list_freelist(&userids->start);
	  return -1;
	}
    }
  
  return 0;
}



/* sends bounces to everyone in this list */
int send_bounces(struct list *bounces, u64_t len, const char *bouncepath, 
		 const char *sendmail, const char *postmaster, const char *fromaddr)
{
  struct element *el;
  FILE *bouncepipe;

  if (!bouncepath)
    {
      trace(TRACE_INFO,"send_bounces(): nothing to bounce!");
      return 0;
    }

  el = list_getstart(bounces);

  while (el)
    {
      bouncepipe = popen(sendmail, "w");
      if (!bouncepipe)
	return -1;

      trace(TRACE_DEBUG, "send_bounces(): sending bounce to [%s] for [%s]", bouncepath, (char*)el->data);

      fprintf(bouncepipe,"From: %s\n", postmaster);
      fprintf(bouncepipe,"To: %s\n", bouncepath);
      fprintf(bouncepipe,"Subject: DBMAIL: delivery failure\n");
      fprintf(bouncepipe,"\n");
      fprintf(bouncepipe,"This is the DBMAIL-SMTP program.\n\n");
      fprintf(bouncepipe,"I'm sorry to inform you that your message, addressed to %s,\n",
	      (char*)el->data);
      fprintf(bouncepipe,"could not be delivered due to the following error.\n\n");

      fprintf(bouncepipe,"*** E-mail address %s is not known here. ***\n\n", (char*)el->data);
      fprintf(bouncepipe,"If you think this message is incorrect please contact %s.\n\n", postmaster);
      fprintf(bouncepipe,"Header of your message follows...\n\n\n");
      fprintf(bouncepipe,"--- header of your message ---\n");
      
      blk[len] = 0;
      fprintf(bouncepipe,"%s",blk);
      fprintf(bouncepipe,"--- end of header ---\n\n\n");

      fprintf(bouncepipe,"\n.\n");
      pclose(bouncepipe);
  
      el = el->nextnode;
    }

  return 0;
}


int send_quotum_bounce(const char *bouncepath, const char *sendmail, const char *postmaster, const char *fromaddr)
{
  FILE *bouncepipe;

  if (!bouncepath)
    {
      trace(TRACE_INFO,"send_bounces(): nothing to bounce!");
      return 0;
    }

  bouncepipe = popen(sendmail, "w");
  if (!bouncepipe)
    return -1;

  trace(TRACE_DEBUG, "send_bounces(): sending bounce to [%s]", bouncepath);

  fprintf(bouncepipe,"From: %s\n", postmaster);
  fprintf(bouncepipe,"To: %s\n", bouncepath);
  fprintf(bouncepipe,"Subject: DBMAIL: delivery failure\n");
  fprintf(bouncepipe,"\n");
  fprintf(bouncepipe,"This is the DBMAIL-SMTP program.\n\n");
  fprintf(bouncepipe,"I'm sorry to inform you that your message\n");
  fprintf(bouncepipe,"could not be delivered due to the following error.\n\n");

  fprintf(bouncepipe,"*** QUOTUM EXCEEDED FOR RECEIVING USER ***\n");
  fprintf(bouncepipe,"If you think this message is incorrect please contact %s.\n\n", postmaster);
  fprintf(bouncepipe,"Header of your message follows...\n\n\n");
  fprintf(bouncepipe,"--- header of your message ---\n");
      
  header[hdrlen] = 0;
  fprintf(bouncepipe,"%s",header);
  fprintf(bouncepipe,"--- end of header ---\n\n\n");

  fprintf(bouncepipe,"\n.\n");
  pclose(bouncepipe);
  
  return 0;
}

