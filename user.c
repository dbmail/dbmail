/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * This is the dbmail-user program
 * It makes adding users easier */

#include "user.h"

/* valid characters for passwd/username */
const char ValidChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"_.!@#$%^&*()-+=~[]{}<>:;";

void show_help();

int do_add(int argc, char *argv[]);
int do_change(int argc, char *argv[]);
int do_delete(char *name);
int do_show(char *name);

int is_valid(const char *name);

int main(int argc, char *argv[])
{
  openlog(PNAME, LOG_PID, LOG_MAIL);
	
  setvbuf(stdout,0,_IONBF,0);
	
  printf ("\n*** dbmail-adduser ***\n");
	
  if (argc<2)
    {
      show_help();
      return 0;
    }
  
  printf ("Opening connection to the database... ");

  if (db_connect()==-1)
    {
      printf ("Failed. Could not connect to database (check log)\n");
      return -1;
    }
	
  printf ("Ok. Connected\n");

  switch (argv[1][0])
    {
    case 'a': do_add(argc-2,&argv[2]); break;
    case 'c': do_change(argc-2,&argv[2]); break;
    case 'd': do_delete(argv[2]); break;
    case 's': do_show(argv[2]); break;
    default:
      show_help();
      db_disconnect();
      return 0;
    }

	
  db_disconnect();
  return 0;
}



int do_add(int argc, char *argv[])
{
  unsigned long useridnr;
  int i;

  if (argc < 4)
    {
      printf ("invalid number of options specified. Check the man page\n");
      return -1;
    }

  if (!is_valid(argv[0]))
    {
      printf("Error: invalid characters in username [%s] encountered\n",argv[0]);
      return -1;
    }

  printf ("Adding user %s with password %s, %s bytes mailbox limit and clientid %s...",
	  argv[0], argv[1], argv[3], argv[2]);

  useridnr = db_adduser (argv[0],argv[1],argv[2],argv[3]);
	
  if (useridnr == -1)
    {
      printf ("Failed\n\nCheck logs for details\n\n");
      return -1;
    }
	
  printf ("Ok, user added id [%lu]\n",useridnr);
	
  for (i = 4; i<argc; i++)
    {
      printf ("Adding alias %s...",argv[i]);
      if (db_addalias(useridnr,argv[i],atoi(argv[2]))==-1)
	printf ("Failed\n");
      else
	printf ("Ok, added\n");
    }
		
  printf ("adduser done\n");

  return 0;
}


int do_change(int argc, char *argv[])
{
  int i;
  unsigned long newsize,userid,newcid;
  char *endptr;

  /* verify the existence of this user */
  userid = db_user_exists(argv[0]);
  if (userid == -1)
    {
      printf("Error verifying existence of user [%s]. Please check the log.\n",argv[0]);
      return -1;
    }

  if (userid == 0)
    {
      printf("Error: user [%s] does not exist.\n",argv[0]);
      return -1;
    }

  printf("Performing changes for user [%s]...",argv[0]);

  for (i=1; argv[i]; i++)
    {
      if (argv[i][0] != '-' && argv[i][0] != '+')
	{
	  printf ("Failed: invalid option specified. Check the man page\n");
	  return -1;
	}
      
      switch (argv[i][1])
	{
	case 'u':
	  /* change the name */
	  if (!is_valid(argv[i+1]))
	    printf("\nWarning: username contains invalid characters. Username not updated.");

	  if (db_change_username(userid,argv[i+1]) != 0)
	    printf("\nWarning: could not change username");

	  i++;
	  break;

	case 'p':
	  /* change the password */
	  if (!is_valid(argv[i+1]))
	    printf("\nWarning: password contains invalid characters. Password not updated.");

	  if (db_change_password(userid,argv[i+1]) != 0)
	    printf("\nWarning: could not change password");

	  i++;
	  break;

	case 'c':
	  newcid = strtoul(argv[i+1], 0, 10);

	  if (db_change_clientid(userid, newcid) != 0)
	    printf("\nWarning: could not change client id");

	  i++;
	  break;
	  
	case 'q':
	  newsize = strtoul(argv[i+1], &endptr, 10);
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

	  if (db_change_mailboxsize(userid, newsize) != 0)
	    printf("\nWarning: could not change max mailboxsize");

	  i++;
	  break;
	      
	case 'a':
	  if (argv[i][0] == '-')
	    {
	      /* remove alias */
	      if (db_removealias(userid, argv[i+1]) < 0)
		printf("\nWarning: could not remove alias [%s]",argv[i+1]);
	    }
	  else
	    {
	      /* add alias */
	      if (db_addalias(userid, argv[i+1], db_getclientid(userid)) < 0)
		printf("\nWarning: could not add alias [%s]",argv[i+1]);
	    }
	  i++;
	  break;

	default:
	  printf ("invalid option specified. Check the man page\n");
	  return -1;
	}
      
    }

  printf("Done\n");
  return 0;
}


int do_delete(char *name)
{
  int result;

  printf("Deleting user [%s]...",name);

  result = db_delete_user(name);

  if (result < 0)
    {
      printf("Failed. Please check the log\n");
      return -1;
    }

  printf("Done\n");
  return 0;
}


int do_show(char *name)
{
  unsigned long userid,cid,quotum;
  struct list userlist;
  struct element *tmp;

  if (!name)
    {
      /* show all users */
      printf("Existing users:\n");
      db_get_known_users(&userlist);
      
      tmp = list_getstart(&userlist);
      while (tmp)
	{
	  printf("[%s]\n", (char*)tmp->data);
	  tmp = tmp->nextnode;
	}

      if (userlist.start)
	list_freelist(&userlist.start);
    }
  else
    {
      printf("Info for user [%s]\n",name);

      userid = db_user_exists(name);
      if (userid == -1)
	{
	  printf("Error verifying existence of user [%s]. Please check the log.\n",name);
	  return -1;
	}

      if (userid == 0)
	{
	  printf("Error: user [%s] does not exist.\n",name);
	  return -1;
	}

      cid = db_getclientid(userid);
      quotum = db_getmaxmailsize(userid);

      printf("Client ID: %lu\n",cid);
      printf("Max. mailboxsize: %lu bytes\n",quotum);

      printf("Aliases:\n");
      db_get_user_aliases(userid, &userlist);

      tmp = list_getstart(&userlist);
      while (tmp)
	{
	  printf("%s\n",(char*)tmp->data);
	  tmp = tmp->nextnode;
	}

      if (userlist.start)
	list_freelist(&userlist.start);
    }

  return 0;
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

  printf("Use this program to manage the users for your dbmail system.\n");
  printf("See the man page for more info. Summary:\n\n");
  printf("dbmail-adduser <a|d|c|s> [username] [options...]\n\n");

}
