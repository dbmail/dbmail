#include <stdio.h>
#include <string.h>
#include "dbmysql.h"
#include "config.h"

#define LINE_BUFFER_SIZE 255

main (int argc, char *argv[])
{
	FILE *configfile;
	char *readbuf, *field, *value;
	int i;
	
	readbuf = (char *)malloc(LINE_BUFFER_SIZE);
	
	printf("*** dbmail-config ***\n\n");
	printf("reading configuration for %s...\n", argv[1]);
	configfile = fopen(argv[1],"r"); /* open the configuration file */
	if (configfile == NULL) /* error test */
	{
		fprintf (stderr,"Error: can not open input file %s\n",argv[1]);
		exit(8);
	}
	
	i = 0;
	
	while (!feof(configfile))
	{
		fgets (readbuf, LINE_BUFFER_SIZE,configfile);
		if (readbuf != NULL)
		{
			i++;
			readbuf[strlen(readbuf)-1]='\0';
			if ((readbuf[0] != '#') && (strlen(readbuf)>3)) /* ignore comments */
			{
				value = strchr(readbuf, '=');
				field = readbuf;
				if (value == NULL)
					fprintf (stderr,"error in line: %d\n",i);
				else
				{
					*value='\0';
					value++;
					/* while ((value[0] != '\0') && ((value[0] == ' ') || (value[0] == '=')))
						value++; */
					fprintf (stdout,"FIELD [%s], VALUE [%s]\n",field, value);
				}
			}
		}
		else
			fprintf (stderr,"end of buffer\n");
		
	}
	return 0;	
}
