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

/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * main file for dbmail-smtp  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "main.h"
#include "pipe.h"
#include "list.h"
#include "auth.h"
#include "header.h"
#include "dsn.h"
#include <string.h>
#include <stdlib.h>
/* For getopt() */
#include <unistd.h>
/* For exit codes */
#include <sysexits.h>

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* syslog */
#define PNAME "dbmail/smtp"

struct list returnpath;         /* returnpath (should aways be just 1 hop) */
struct list mimelist; 	        /* raw unformatted mimefields and values */
struct list dsnusers;           /* list of deliver_to_user_t structs */
struct list users;              /* list of email addresses in message */
struct element *tmp;

struct list sysItems, smtpItems; /* config item lists */

char *configFile = DEFAULT_CONFIG_FILE;

extern db_param_t _db_params;   /* set up database login data */

deliver_to_user_t dsnuser;

char *header = NULL;
char *deliver_to_header = NULL;
char *deliver_to_mailbox = NULL;
u64_t headersize;
u64_t newlines;

void print_usage(const char *progname)
{
  printf("\n*** DBMAIL: dbmail-smtp version $Revision$ %s\n",
             COPYRIGHT);
  printf("\nUsage: %s -n [headerfield]   for normal deliveries (default: \"deliver-to\")\n",
             progname);
  printf("       %s -m \"mailbox\" -u [username] for delivery to mailbox (name)\n",
             progname);
  printf("       %s -d [addresses]  for delivery without using scanner\n",
             progname);
  printf("       %s -u [usernames]  for direct delivery to users\n\n",
             progname);
}

int main(int argc, char *argv[]) 
{
  int exitcode = 0;
  int c, c_prev = 0, usage_error = 0;
  u64_t dummyidx = 0, dummysize = 0;
  int has_2 = 0, has_4 = 0, has_5 = 0;


  openlog(PNAME, LOG_PID, LOG_MAIL);

  ReadConfig("DBMAIL", configFile, &sysItems);
  ReadConfig("SMTP", configFile, &smtpItems);
  SetTraceLevel(&smtpItems);
  GetDBParams(&_db_params, &sysItems);

  list_init(&users);
  list_init(&dsnusers);
  list_init(&mimelist);
  list_init(&returnpath);

  /* Check for commandline options.
   * The initial '-' means that arguments which are not associated
   * with an immediately preceding option are return with option 
   * value '1'. We will use this to allow for multiple values to
   * follow after each of the supported options. */
  while ((c = getopt(argc, argv, "-n::m:u:d:f:")) != EOF)
    {
      /* Received an n-th value following the last option,
       * so recall the last known option to be used in the switch. */
      if (c == '1')
          c = c_prev;
      c_prev = c;
      /* Do something with this option. */
      switch (c)
        {
          case 'n':
              trace(TRACE_INFO, "main(): using NORMAL_DELIVERY");
             
              if (optarg)
                {
                  if (deliver_to_header)
                    {
                      printf("Only one header field may be specified.\n");
                      usage_error = 1;
                    }
                  else
                      deliver_to_header = optarg;
                }
              else
                  deliver_to_header = "deliver-to";

              break;
          case 'm':
              trace(TRACE_INFO, "main(): using SPECIAL_DELIVERY to mailbox");

              if (deliver_to_mailbox)
                {
                  printf("Only one header field may be specified.\n");
                  usage_error = 1;
                }
              else
                  deliver_to_mailbox = optarg;

              break;
          case 'f':
              trace(TRACE_INFO, "main(): using RETURN_PATH for bounces");

              /* Add argument onto the returnpath list. */
              if (list_nodeadd(&returnpath, optarg, strlen(optarg) + 1) == 0)
                {
                  trace(TRACE_ERROR, "main(): list_nodeadd reports out of memory"
                                  " while adding to returnpath");
                  exitcode = EX_TEMPFAIL;
                  goto freeall;
                }

              break;
          case 'u':
              trace(TRACE_INFO, "main(): using SPECIAL_DELIVERY to usernames");

              dsnuser_init(&dsnuser);
              dsnuser.address = strdup(optarg);

              /* Add argument onto the users list. */
              if (list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t)) == 0)
                {
                  trace(TRACE_ERROR, "main(): list_nodeadd reports out of memory"
                                  " while adding usernames");
                  exitcode = EX_TEMPFAIL;
                  goto freeall;
                }

              break;
          case 'd':
              trace(TRACE_INFO, "main(): using SPECIAL_DELIVERY to email addresses");

              dsnuser_init(&dsnuser);
              dsnuser.address = strdup(optarg);

              /* Add argument onto the users list. */
              if (list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t)) == 0)
                {
                  trace(TRACE_ERROR, "main(): list_nodeadd reports out of memory"
                                  " while adding email addresses");
                  exitcode = EX_TEMPFAIL;
                  goto freeall;
                }

              break;
          default:
              usage_error = 1;
              break;
        }

    /* At the end of each round of options, check
     * to see if there were any errors worth stopping for. */
    if (usage_error)
      {
        print_usage(argv[0]);
        trace(TRACE_DEBUG, "main(): usage error; setting EX_USAGE and aborting");
        exitcode = EX_USAGE;
        goto freeall;
      }
    }

  /* ...or if there weren't any command line arguments at all. */
  if (argc < 2)
    {
      print_usage(argv[0]);
      trace(TRACE_DEBUG, "main(): no arguments; setting EX_USAGE and aborting");
      exitcode = EX_USAGE;
      goto freeall;
    }

  if (db_connect() != 0) 
    {
      trace(TRACE_ERROR, "main(): database connection failed");
      exitcode = EX_TEMPFAIL;
      goto freeall;
    }

  if (auth_connect() != 0) 
    {
      trace(TRACE_ERROR, "main(): authentication connection failed");
      exitcode = EX_TEMPFAIL;
      goto freeall;
    }

  /* first we need to read the header */
  if (!read_header(stdin, &newlines, &headersize, &header))
    {
      trace(TRACE_ERROR, "main(): read_header failed to read a header");
      exitcode = EX_TEMPFAIL;
      goto freeall;
    }

  /* parse the list and scan for field and content */
  if (mime_readheader(header, &dummyidx, &mimelist, &dummysize) < 0)
    {
      trace(TRACE_ERROR, "main(): mime_readheader failed to read a header list");
      exitcode = EX_TEMPFAIL;
      goto freeall;
    }

  /* parse returnpath from header */
  if (returnpath.total_nodes == 0)
      mail_adr_list("Return-Path", &returnpath, &mimelist);
      if (returnpath.total_nodes == 0)
          mail_adr_list("From", &returnpath, &mimelist);
          if (returnpath.total_nodes == 0)
              trace(TRACE_DEBUG, "main(): no return path found.");

  /* If the NORMAL delivery mode has been selected... */
  if (deliver_to_header != NULL)
    {
      /* parse for destination addresses */
      trace(TRACE_DEBUG, "main(): scanning mimelist for header [%s]", deliver_to_header);
      if (mail_adr_list(deliver_to_header, &users, &mimelist) !=0)
        {
          trace(TRACE_ERROR, "main(): scanner found no email addresses (scanned for %s)", 
                deliver_to_header);
          exitcode = EX_TEMPFAIL;
          goto freeall;
        }
  
      /* Loop through the users list, moving the entries into the dsnusers list. */
      for(tmp = list_getstart(&users); tmp != NULL; tmp = tmp->nextnode)
        {
          deliver_to_user_t dsnuser;
  
          dsnuser_init(&dsnuser);
          dsnuser.address = strdup((char *)tmp->data);

          if (list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t)) == 0)
            {
              trace(TRACE_ERROR, "main(): list_nodeadd reports out of memory"
                              " while adding to dsnusers");
              exitcode = EX_TEMPFAIL;
	      /* If the struct was not added to the list,
	       * then it won't be released for us at freeall.*/
	      dsnuser_free(&dsnuser);
              goto freeall;
            }
        }
    }

  /* If the MAILBOX delivery mode has been selected... */
  if (deliver_to_mailbox)
    {
      /* Loop through the dsnusers list, setting the target mailbox. */
      for(tmp = list_getstart(&dsnusers); tmp != NULL; tmp = tmp->nextnode)
        {
          deliver_to_user_t *dsnuser;

	  dsnuser = (deliver_to_user_t *)tmp->data;
      
          dsnuser->mailbox = strdup(deliver_to_mailbox);
        }
    }

  /* inserting messages into the database */
  if (insert_messages(stdin, header, headersize, &mimelist, &dsnusers, &returnpath) == -1)
    {
      trace(TRACE_ERROR, "main(): insert_messages failed");
      /* Most likely a random failure... */
      exitcode = EX_TEMPFAIL;
    }

freeall: /* Goto's here! */

  /* Get one reasonable error code for everyone. */
  for(tmp = list_getstart(&dsnusers); tmp != NULL; tmp = tmp->nextnode)
    {
      deliver_to_user_t *dsnuser= (deliver_to_user_t *)tmp->data;

      switch (dsnuser->dsn.class)
      {
        case 2:
          /* Success. */
          has_2 = 1;
          break;
        case 4:
          /* Temporary transient failure. */
          has_4 = 1;
          break;
        case 5:
          /* Permanent failure. */
          has_5 = 1;
          break;
      }
      
      dsnuser_free(dsnuser);
    }

  /* If there wasn't already an EX_TEMPFAIL from insert_messages(),
   * then see if one of the status flags was marked with an error. */
  if (!exitcode)
    {
      /* If only one code, use it. */
      if (has_2 && !has_4 && !has_5) /* Only 2 */
          exitcode = EX_OK;
      else if (!has_2 && has_4 && !has_5) /* Only 4 */
          exitcode = EX_TEMPFAIL;
      else if (!has_2 && !has_4 && has_5) /* Only 5 */
          exitcode = EX_NOUSER;
      /* If two codes, prefer temporary. */
      else if (has_2 && has_4 && !has_5) /* 2 and 4 */
          exitcode = EX_TEMPFAIL;
      else if (has_2 && !has_4 && has_5) /* 2 and 5 */
          exitcode = EX_NOUSER;
      else if (!has_2 && has_4 && has_5) /* 4 and 5 */
          exitcode = EX_TEMPFAIL;
      else /* All 3 */
          exitcode = EX_UNAVAILABLE;
    }

  trace(TRACE_DEBUG, "main(): freeing memory blocks");
  if (header != NULL)
      my_free(header);
  list_freelist(&sysItems.start);
  list_freelist(&smtpItems.start);
  list_freelist(&mimelist.start);
  list_freelist(&returnpath.start);
  list_freelist(&dsnusers.start);
  list_freelist(&users.start);
  trace(TRACE_DEBUG, "main(): they're all free. we're done.");
  
  db_disconnect();
  auth_disconnect();

  trace(TRACE_DEBUG, "main(): exit code is [%d].", exitcode);
  return exitcode;
}

