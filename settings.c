/* $Id$

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "dbmail.h"
#include "list.h"
#include "debug.h"
#include "db.h"

#define LINE_BUFFER_SIZE 516

int main(int argc, char *argv[])
{
	FILE *configfile;
	char *readbuf, *field, *val, *fname;
	int i;

	if(!(readbuf = (char *) dm_malloc(LINE_BUFFER_SIZE))) {
		printf("Error allocating memory. Exiting..\n");
		return 1;
	}

	printf("*** dbmail-config ***\n\n");
	if (argc < 2) {
		printf("No configuration file specified, using %s\n",
		       DEFAULT_CONFIG_FILE);
		fname = DEFAULT_CONFIG_FILE;
	} else {
		fname = argv[1];
	}

	if (db_connect() != 0) {
		printf("Could not connect to database.\n");
		dm_free(readbuf);
		return -1;
	}

	if (auth_connect() != 0) {
		printf("Could not connect to authentication.\n");
		dm_free(readbuf);
		return -1;
	}

	printf("reading configuration for %s...\n", fname);
	configfile = fopen(fname, "r");	/* open the configuration file */
	if (configfile == NULL) {	/* error test */
		fprintf(stderr, "Error: can not open input file %s\n",
			fname);
		dm_free(readbuf);
		return 8;
	}

	i = 0;

	/* clear existing configuration */
	db_clear_config();

	while (!feof(configfile)) {
		fgets(readbuf, LINE_BUFFER_SIZE, configfile);
		if (readbuf != NULL) {
			i++;
			readbuf[strlen(readbuf) - 1] = '\0';
			if ((readbuf[0] != '#') && (strlen(readbuf) > 3)) {	/* ignore comments */
				val = strchr(readbuf, '=');
				field = readbuf;
				if (!val) {
					fprintf(stderr,
						"Configread error in line: %d\n",
						i);
				} else {
					*val = '\0';
					val++;
					if (db_insert_config_item
					    (field, val) != 0)
						fprintf(stderr,
							"error in line:%d, could not insert item\n",
							i);
					else
						printf
						    ("%s is now set to %s\n",
						     field, val);
				}
			}
		} else
			fprintf(stderr, "end of buffer\n");

	}
	dm_free(readbuf);
	return 0;
}
