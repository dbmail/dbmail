/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * This is the dbmail-user program
 * It makes adding users easier */

#include "user.h"
#include "auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "list.h"
#include "debug.h"
#include "db.h"
#include <crypt.h>
#include <time.h>
#include <stdarg.h>

char *configFile = "/etc/dbmail.conf";

#define SHADOWFILE "/etc/shadow"

char *getToken(char** str,const char* delims);
char csalt[] = "........";
char *bgetpwent (char *filename, char *name);
char *cget_salt();

/* database login data */
extern field_t _db_host;
extern field_t _db_db;
extern field_t _db_user;
extern field_t _db_pass;


/* valid characters for passwd/username */
const char ValidChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"_.!@#$%^&*()-+=~[]{}<>:;\\/";

void show_help();
int quiet = 0;

int quiet_printf(const char *fmt, ...);

int do_add(int argc, char *argv[]);
int do_change(int argc, char *argv[]);
int do_delete(char *name);
int do_show(char *name);
int do_empty(char *name);
int do_make_alias(char *argv[]);
int do_remove_alias(char *argv[]);

int is_valid(const char *name);

int main(int argc, char *argv[])
{
  struct list sysItems;
  int result;
  int argidx = 0;

  openlog(PNAME, LOG_PID, LOG_MAIL);
	
  setvbuf(stdout,0,_IONBF,0);
	
  if (argc<2)
    {
      show_help();
      return 0;
    }
  
  if (strcasecmp(argv[1], "quiet") == 0)
    {
      if (argc < 3)
	{
	  show_help();
	  return 0;
	}

      quiet = 1;
      argidx = 1;
    }
	    
  ReadConfig("DBMAIL", configFile, &sysItems);
  SetTraceLevel(&sysItems);
  GetDBParams(_db_host, _db_db, _db_user, _db_pass, &sysItems);

  quiet_printf ("\n*** dbmail-adduser ***\n");
	
  quiet_printf ("Opening connection to the database... ");
  if (db_connect()==-1)
    {
      quiet_printf ("Failed. Could not connect to database (check log)\n");
      return -1;
    }
	
/*  quiet_printf ("Opening connection to the user database... ");
  if (auth_connect()==-1)
    {
      quiet_printf ("Failed. Could not connect to user database (check log)\n");
      db_disconnect();
      return -1;
    }
*/

  quiet_printf ("Ok. Connected\n");
  configure_debug(TRACE_ERROR, 1, 0);
  
  switch (argv[argidx+1][0])
    {
    case 'a': result = do_add(argc- (2+argidx),&argv[2+argidx]); break;
    case 'c': result = do_change(argc- (2+argidx),&argv[2+argidx]); break;
    case 'd': result = do_delete(argv[2+argidx]); break;
    case 's': result = do_show(argv[2+argidx]); break;
    case 'f': result = do_make_alias(&argv[2+argidx]); break;
    case 'x': result = do_remove_alias(&argv[2+argidx]); break;
    case 'e': result = do_empty(argv[2+argidx]); break;
    default:
      show_help();
      db_disconnect();
//      auth_disconnect();
      return 0;
    }

	
  db_disconnect();
//  auth_disconnect();
  return result;
}



/* 
 * adds a single alias (not connected to any user) 
 */
int do_make_alias(char *argv[])
{
  int result;

  if (!argv[0] || !argv[1])
    {
      quiet_printf ("invalid arguments specified. Check the man page\n");
      return -1;
    }

  quiet_printf("Adding alias [%s] --> [%s]...", argv[0], argv[1]);
  switch  ( (result = db_addalias_ext(argv[0], argv[1], 0)) )
    {
    case -1:
      quiet_printf("Failed\n\nCheck logs for details\n\n");
      break;

    case 0:
      quiet_printf("Ok alias added\n");
      break;

    case 1:
      quiet_printf("Already exists. no extra alias added\n");
      result = -1; /* return error */
      break;

    }

  return result;
}

int do_remove_alias(char *argv[])
{
  if (!argv[0] || !argv[1])
    {
      quiet_printf ("invalid arguments specified. Check the man page\n");
      return -1;
    }

  quiet_printf("Removing alias [%s] --> [%s]...", argv[0], argv[1]);
  if (db_removealias_ext(argv[0], argv[1]) != 0)
    {
      quiet_printf("Failed\n\nCheck logs for details\n\n");
      return -1;
    }

  quiet_printf("Ok alias removed\n");
  return 0;
}

int do_add(int argc, char *argv[])
{
  u64_t useridnr;
  int i, result;
  char pw[50]="";

  if (argc < 4)
    {
      quiet_printf ("invalid number of options specified. Check the man page\n");
      return -1;
    }

  if (!is_valid(argv[0]))
    {
      quiet_printf("Error: invalid characters in username [%s] encountered\n",argv[0]);
      return -1;
    }

  quiet_printf ("Adding user %s with password %s, %s bytes mailbox limit and clientid %s...",
	  argv[0], argv[1], argv[3], argv[2]);

  /* check if we need to encrypt this pwd */
  if (strncasecmp(argv[1], "{crypt:}", strlen("{crypt:}")) == 0)
    {
      /* encrypt  using crypt() */
      strcat(pw,crypt(&argv[1][strlen("{crypt:}")], cget_salt()));
      useridnr = auth_adduser(argv[0], pw, "crypt",argv[2],argv[3]);
    }
  else if (strncasecmp(argv[1], "{crypt}", strlen("{crypt}")) == 0)
    {
      /* assume passwd is encrypted on command line */
      useridnr = auth_adduser(argv[0], &argv[1][strlen("{crypt}")], "crypt",argv[2],argv[3]);
    }
  else
    {
      useridnr = auth_adduser(argv[0],argv[1],"",argv[2],argv[3]);
    }

  if (useridnr == -1)
    {
      /* check if the existence of the user caused the failure */
      if ( (useridnr = auth_user_exists(argv[0])) != 0 )
	{
	  if (useridnr == -1)
	    {
	      quiet_printf ("Failed\n\nCheck logs for details\n\n");
	      return -1;  /* dbase failure */
	    }
	  
	  quiet_printf("Failed: user exists [%llu]\n", useridnr);
	}
      else
	{
	  quiet_printf ("Failed\n\nCheck logs for details\n\n");
	  useridnr = -1;
	}

      return -1;
    }
	
  quiet_printf ("Ok, user added id [%llu]\n",useridnr);

  for (i = 4, result = 0; i<argc; i++)
    {
      quiet_printf ("Adding alias %s...",argv[i]);
      switch ( db_addalias(useridnr,argv[i],atoi(argv[2])) )
	{
	case -1:
	  quiet_printf ("Failed\n");
	  result = -1;
	  break;
	  
	case 0:
	  quiet_printf ("Ok, added\n");
	  break;

	case 1:
	  quiet_printf("Already exists. No extra alias added\n");
	  result = -1;
	  break;
	}
    }
		
  quiet_printf ("adduser done\n");
  if (result != 0)
    quiet_printf("Warning: user added but not all the specified aliases\n");

  return result;
}


int do_change(int argc, char *argv[])
{
  int i,result = 0, retval=0;
  u64_t newsize,userid,newcid;
  char *endptr = NULL,*entry = NULL,*passwdfile = NULL;
  char pw[50]="";

  /* verify the existence of this user */
  userid = auth_user_exists(argv[0]);
  if (userid == -1)
    {
      quiet_printf("Error verifying existence of user [%s]. Please check the log.\n",argv[0]);
      return -1;
    }

  if (userid == 0)
    {
      quiet_printf("Error: user [%s] does not exist.\n",argv[0]);
      return -1;
    }

  quiet_printf("Performing changes for user [%s]...",argv[0]);

  for (i=1; argv[i]; i++)
    {
      if (argv[i][0] != '-' && argv[i][0] != '+' && argv[i][0] != 'x')
	{
	  quiet_printf ("Failed: invalid option specified. Check the man page\n");
	  return -1;
	}
      
      switch (argv[i][1])
	{
	case 'u':
	  /* change the name */
	  if (!is_valid(argv[i+1]))
	    {
	      quiet_printf("\nWarning: username contains invalid characters. Username not updated. ");
	      retval = -1;
	    }

	  if (auth_change_username(userid,argv[i+1]) != 0)
	    {
	      quiet_printf("\nWarning: could not change username ");
	      retval = -1;
	    }

	  i++;
	  break;

	case 'p':
	  /* change the password */
	  if (!is_valid(argv[i+1]))
	    {
	      quiet_printf("\nWarning: password contains invalid characters. Password not updated. ");
	      retval = -1;
	    }

          switch (argv[i][0])
	    {
	    case '+':
	      /* +p will converse clear text into crypt hash value */
	      strcat(pw,crypt(argv[i+1], cget_salt()));
	      result = auth_change_password(userid,pw,"crypt");
	      break;
	    case '-':
	      strcpy(pw,argv[i+1]);
	      result = auth_change_password(userid,pw,"");
	      break;
	    case 'x':
	      /* 'xp' will copy passwd from command line 
	        assuming that the supplied passwd is crypt encrypted 
	      */
	      strcpy(pw,argv[i+1]);
	      result = auth_change_password(userid,pw,"crypt");
	      break;
	    }

	  if (result != 0)
	    {
	      quiet_printf("\nWarning: could not change password ");
	      retval = -1;
	    }

	  i++;
	  break;

        case 'P':
          /* -P will copy password from SHADOWFILE */
	  /* -P:filename will copy password from filename */
	  if (argv[i][2] == ':')
	    passwdfile = &argv[i][3];
	  else
	    passwdfile = SHADOWFILE;
	      

	  entry = bgetpwent(passwdfile, argv[0]);
	  if (!entry)
	    {
	      quiet_printf("\nWarning: error finding password from [%s] - are you superuser?\n", 
			   passwdfile);
	      retval = -1;
	      break;
	    }
	     
          strncat(pw,entry,50);
          if ( strcmp(pw, "") == 0 ) 
	    {
	      quiet_printf("\n%s's password not found at \"%s\" !\n", argv[0],passwdfile);
	      retval = -1;
	    } 
	  else 
	    {
	      if (auth_change_password(userid,pw,"crypt") != 0)
		{
		  quiet_printf("\nWarning: could not change password");
		  retval = -1;
		}
	    }
          break;

	case 'c':
	  newcid = strtoull(argv[i+1], 0, 10);

	  if (auth_change_clientid(userid, newcid) != 0)
	    {
	      quiet_printf("\nWarning: could not change client id ");
	      retval = -1;
	    }

	  i++;
	  break;
	  
	case 'q':
	  newsize = strtoull(argv[i+1], &endptr, 10);
	  switch (*endptr)
	    {
	    case 'm':
	    case 'M':
	      newsize *= 1000000;
	      break;

	    case 'k':
	    case 'K':
	      newsize *= 1000;
	      break;
	    }

	  if (auth_change_mailboxsize(userid, newsize) != 0)
	    {
	      quiet_printf("\nWarning: could not change max mailboxsize ");
	      retval = -1;
	    }

	  i++;
	  break;
	      
	case 'a':
	  if (argv[i][0] == '-')
	    {
	      /* remove alias */
	      if (db_removealias(userid, argv[i+1]) < 0)
		{
		  quiet_printf("\nWarning: could not remove alias [%s] ",argv[i+1]);
		  retval = -1;
		}
	    }
	  else
	    {
	      /* add alias */
	      if (db_addalias(userid, argv[i+1], auth_getclientid(userid)) < 0)
		{
		  quiet_printf("\nWarning: could not add alias [%s]",argv[i+1]);
		  retval = -1;
		}
	    }
	  i++;
	  break;

	default:
	  quiet_printf ("invalid option specified. Check the man page\n");
	  return -1;
	}
      
    }

  quiet_printf("Done\n");

  return retval;
}


int do_delete(char *name)
{
  int result;

  quiet_printf("Deleting user [%s]...",name);

  result = auth_delete_user(name);

  if (result < 0)
    {
      quiet_printf("Failed. Please check the log\n");
      return -1;
    }

  quiet_printf("Done\n");
  return 0;
}


int do_show(char *name)
{
  u64_t userid,cid,quotum;
  struct list userlist;
  struct element *tmp;
  char *deliver_to;

  if (!name)
    {
      /* show all users */
      quiet_printf("Existing users:\n");

      auth_get_known_users(&userlist);
      
      tmp = list_getstart(&userlist);
      while (tmp)
	{
	  quiet_printf("[%s]\n", (char*)tmp->data);
	  tmp = tmp->nextnode;
	}

      if (userlist.start)
	list_freelist(&userlist.start);
    }
  else
    {
      quiet_printf("Info for user [%s]",name);

      userid = auth_user_exists(name);
      if (userid == -1)
	{
	  quiet_printf("\nError verifying existence of user [%s]. Please check the log.\n",name);
	  return -1;
	}

      if (userid == 0)
	{
	  /* 'name' is not a user, try it as an alias */
	  quiet_printf("..is not a user, trying as an alias");

	  deliver_to = db_get_deliver_from_alias(name);
	  
	  if (!deliver_to)
	    {
	      quiet_printf("\nError verifying existence of alias [%s]. Please check the log.\n",name);
	      return -1;
	    }

	  if (deliver_to[0] == '\0')
	    {
	      quiet_printf("..is not an alias.\n");
	      return 0;
	    }

	  userid = strtoul(deliver_to, NULL, 10);
	  if (userid == 0)
	    {
	      quiet_printf("\n[%s] is an alias for [%s]\n", name, deliver_to);
	      my_free(deliver_to);
	      return 0;
	    }

	  my_free(deliver_to);
	  quiet_printf("\nFound user for alias [%s]:\n\n", name);
	}

      cid = auth_getclientid(userid);
      quotum = auth_getmaxmailsize(userid);

      quiet_printf("User ID         : %llu\n", userid);
      quiet_printf("Username        : %s\n", auth_get_userid(&userid));
      quiet_printf("Client ID       : %llu\n",cid);
      quiet_printf("Max. mailboxsize: %llu bytes\n",quotum);
      quiet_printf("Quotum used     : %llu bytes\n", db_get_quotum_used(userid));
      quiet_printf("\n");

      quiet_printf("Aliases:\n");
      db_get_user_aliases(userid, &userlist);

      tmp = list_getstart(&userlist);
      while (tmp)
	{
	  quiet_printf("%s\n",(char*)tmp->data);
	  tmp = tmp->nextnode;
	}

      quiet_printf("\n");
      if (userlist.start)
	list_freelist(&userlist.start);
    }

  return 0;
}


/*
 * empties the mailbox associated with user 'name'
 */
int do_empty(char *name)
{
  u64_t userid = auth_user_exists(name);
  int result;

  if (userid == 0)
    {
      quiet_printf("User [%s] does not exist.\n", name);
      return -1;
    }

  if (userid == -1)
    {
      quiet_printf("Error verifying existence of user [%s]. Please check the log.\n",name);
      return -1;
    }

  quiet_printf("Emptying mailbox..."); fflush(stdout);

  result = db_empty_mailbox(userid);
  if (result != 0)
    quiet_printf("Error. Please check the log.\n",name);
  else
    quiet_printf("Ok.\n");

  return result;
}


int is_valid(const char *name)
{
  int i;
  
  for (i=0; name[i]; i++)
    if (strchr(ValidChars, name[i]) == NULL)
      return 0;

  return 1;
}


void show_help()
{
  printf ("\n*** dbmail-adduser ***\n");
	
  printf("Use this program to manage the users for your dbmail system.\n");
  printf("See the man page for more info. Summary:\n\n");
  printf("dbmail-adduser [quiet] <a|d|c|s|f|x|e> [username] [options...]\n\n");

}


int quiet_printf(const char *fmt, ...)
{
  va_list argp;
  int r;

  if (quiet)
    return 0;
 
  va_start(argp, fmt);
  r = vprintf(fmt, argp);
  va_end(argp);

  return r;
}
  

/*eddy
  This two function was base from "cpu" by Blake Matheny <matheny@dbaseiv.net>
  bgetpwent : get hash password from /etc/shadow
  cget_salt : generate salt value for crypt
*/
char *bgetpwent(char *filename, char *name) 
{
  FILE *passfile = NULL;
  char pass_char[512];
  int pass_size = 511;
  char *pw = NULL;
  char *user = NULL;

  if ((passfile = fopen(filename, "r")) == NULL)
    return NULL;

  while (fgets(pass_char, pass_size, passfile) != NULL) {
    char *m = pass_char;
    int num_tok = 0;
    char *toks;

    while (m != NULL && *m != 0) {
      toks = getToken(&m, ":");
      if (num_tok == 0)
	user = toks;
      else if (num_tok == 1)
                                /*result->pw_passwd = toks;*/
	pw = toks;
      else
	break;
      num_tok++;
    }
    if (strcmp(user, name) == 0)
      return pw;

  }
  return "";
}

char *cget_salt()
{
  unsigned long seed[2];
  const char *const seedchars =
    "./0123456789ABCDEFGHIJKLMNOPQRST"
    "UVWXYZabcdefghijklmnopqrstuvwxyz";
  int i;

  seed[0] = time(NULL);
  seed[1] = getpid() ^ (seed[0] >> 14 & 0x30000);
  for (i = 0; i < 8; i++)
    csalt[i] = seedchars[(seed[i / 5] >> (i % 5) * 6) & 0x3f];

  return csalt;
}


/*
  This function was base on function of "cpu"
        by Blake Matheny <matheny@dbaseiv.net>
  getToken : break down username and password from a file
*/
char *getToken(char** str,const char* delims)
{
  char* token;

  if (*str==NULL) 
    {
      /* No more tokens */
      return NULL;
    }

  token=*str;
  while (**str!='\0') 
    {
      if (strchr(delims,**str)!=NULL) {
	**str='\0';
	(*str)++;
	return token;
      }
      (*str)++;
    }

  /* There is no other token */
  *str=NULL;
  return token;
}
