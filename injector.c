/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * injector.c
 *
 * Code for the SMTP injector program.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "auth.h"
#include "db.h"
#include "debug.h"
#include "list.h"
#include "misc.h"
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
char *blk;
char *header;
unsigned hdrlen;

int add_address(const char *uname, 
		struct list *userids, struct list *bounces, struct list *fwds);
int add_username(const char *uname, struct list *userids);
int process_args(int argc, char *argv[], 
		 struct list *userids, struct list *bounces, struct list *fwds, 
		 char **field_to_scan);

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
  char save, *sub = NULL, *tmp = NULL;
  struct element *el;

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_ERROR, 1, 0);  /* do not spill time on reading settings */

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

  /* open database connection */
  if (db_connect() != 0)
    {
      printf("Error opening database connection\n");
      return -1;
    }

  /* open authentication connection */
  if (auth_connect() != 0)
    {
      printf("Error opening authentication connection\n");
      return -1;
    }

  /* alloc mem */
  if (! (blk = (char*)my_malloc(READ_BLOCK_SIZE + MAX_LINESIZE)) )
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): not enough memory");
      return -1;
    }
      
  if (! (header = (char*)my_malloc(READ_BLOCK_SIZE + MAX_LINESIZE)) )
    {
      db_disconnect();
      auth_disconnect();
      my_free(blk);
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): not enough memory");
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
      my_free(blk);
      my_free(header);
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
      my_free(blk);
      my_free(header);
      trace(TRACE_FATAL, "main(): error processing command line options");
    }

  if ((len = read_header_process(&userids, &bounces, &fwds, field_to_scan, blk, &newlines, &bouncepath)) == -1)
    {
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      trace(TRACE_FATAL, "main(): error processing header");
      my_free(blk);
      my_free(header);
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
      my_free(blk);
      my_free(header);
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
      my_free(blk);
      my_free(header);
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
      my_free(blk);
      my_free(header);
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
	  my_free(blk);
	  my_free(header);
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
      db_disconnect();
      auth_disconnect();
      list_freelist(&userids.start);
      list_freelist(&bounces.start);
      list_freelist(&fwds.start);
      my_free(qleft);
      my_free(uids);
      my_free(blk);
      my_free(header);
      trace(TRACE_FATAL,"main(): error bouncing messages");
      return -1;
    }

  srand((int) ((int) time(NULL) + (int) getpid()));
  create_unique_id(uniqueid, 0);
  
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


  /* swap to save header */
  tmp = blk;
  blk = header;
  header = tmp;

  hdrlen = len;
  header[hdrlen] = 0;


  /* read data & insert */
  size = hdrlen; /* don't forget the header size! */
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
	      my_free(header);
	      my_free(blk);
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
      
      /* count newlines in this block */
      for (i=0; blk[i] && i<len; i++)
	if (blk[i] == '\n') newlines++;
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
  my_free(header);
  my_free(blk);

  return 0;
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
      for (i=2; argv[i]; i++)
	{
	  if (add_address(argv[i], userids, bounces, fwds) != 0)
	    {
	      trace(TRACE_ERROR,"process_args(): error adding address [%s]", argv[i]);
	      return -1;
	    }
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
  else
    {
      trace(TRACE_ERROR,"process_args(): invalid command line option [%s]", argv[1]);
      return -1;
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

