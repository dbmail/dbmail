/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands */

#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "list.h"
#include "debug.h"
#include "db.h"

#define LINE_BUFFER_SIZE 516

int main (int argc, char *argv[])
{
  FILE *configfile;
  char *readbuf, *field, *val,*fname;
  int i;
	
  readbuf = (char *)malloc(LINE_BUFFER_SIZE);
	
  printf("*** dbmail-config ***\n\n");
  if (argc<2)
    {
      printf("No configuration file specified, using %s\n",DEFAULT_CONFIG_FILE);
      fname = DEFAULT_CONFIG_FILE;
    }
  else
    {
      fname = argv[1];
    }

  if (db_connect()==-1)
    {
      printf ("Could not connect to database.\n");
      return -1;
    }
	
  printf("reading configuration for %s...\n", fname);
  configfile = fopen(fname,"r"); /* open the configuration file */
  if (configfile == NULL) /* error test */
    {
      fprintf (stderr,"Error: can not open input file %s\n",fname);
      return 8;
    }
	
  i = 0;
  
  /* clear existing configuration */
  db_clear_config();

  while (!feof(configfile))
    {
      fgets (readbuf, LINE_BUFFER_SIZE,configfile);
      if (readbuf != NULL)
	{
	  i++;
	  readbuf[strlen(readbuf)-1]='\0';
	  if ((readbuf[0] != '#') && (strlen(readbuf)>3)) /* ignore comments */
	    {
	      val = strchr(readbuf, '=');
	      field = readbuf;
	      if (!val)
          {
		    fprintf (stderr,"Configread error in line: %d\n",i);
            }
	      else
		{
		  *val='\0';
		  val++;
		  if (db_insert_config_item (field, val) != 0)
		    fprintf (stderr,"error in line:%d, could not insert item\n",i);
		  else 
		    printf ("%s is now set to %s\n",field,val);
		}
	    }
	}
      else
	fprintf (stderr,"end of buffer\n");
		
    }
  return 0;	
}

