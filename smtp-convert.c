/*
  $Id$
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

/* 
 * this program traverses a directory tree and executes
 * dbmail conversion on each file.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "auth.h"
#include "dbmailtypes.h"
#include "debug.h"
#include <regex.h>

#define MAX_LINESIZE 1024
#define UID_SIZE 70

const char *mbox_delimiter_pattern = "^From .*  ";
char blk[READ_BLOCK_SIZE + MAX_LINESIZE + 1];

/* syslog */
#define PNAME "dbmail/uni-one-convertor"

char *getusername(char *path);
int traverse(char *path);
int process_mboxfile(char *file, u64_t userid);



int main(int argc, char *argv[])
{
	time_t start;
	time_t stop;
	int result;

	if (argc < 2) {
		printf("Error, traverse need a directory as argument\n");
		return -1;
	}

	openlog(PNAME, LOG_PID, LOG_MAIL);	/* open connection to syslog */
	configure_debug(TRACE_ERROR, 1, 0);

	/* open database connection */
	if (db_connect() != 0) {
		printf("Error opening database connection\n");
		return -1;
	}

	/* open authentication connection */
	if (auth_connect() != 0) {
		printf("Error opening authentication connection\n");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));

	time(&start);		/* mark the starting time */
	result = traverse(argv[1]);
	time(&stop);		/* mark the ending time */

	printf("Conversion started @  %s", ctime(&start));
	printf("Conversion finished @ %s", ctime(&stop));

	return result;
}



char *getusername(char *path)
{
	int i;
	char *tmp;

	i = strlen(path);
	tmp = path + i;

	while ((tmp != path) && (*tmp != '/'))
		tmp--;

	return tmp + 1;
}


int traverse(char *path)
{
	char newpath[1024];
	char *username;
	struct dirent **namelist;
	int n;
	u64_t userid;

	n = scandir(path, &namelist, 0, alphasort);

	if (n < 0) {
		printf("file %s\n", path);
		username = getusername(path);
		printf("username %s\n", username);

		printf("creating user...");
		userid = auth_adduser(username, "default", "", "0", "10M");
		if (userid != -1 && userid != 0) {
			printf("Ok id [%llu]\n", userid);
			printf("converting mailbox...");
			fflush(stdout);
			n = process_mboxfile(path, userid);
			if (n != 0)
				printf
				    ("Warning: error converting mailbox\n");
			else
				printf("done :)\n");
		} else {
			printf("user already exists. Skipping\n");
		}

	} else {
		while (n--) {
			if ((strcmp(namelist[n]->d_name, "..") != 0) &&
			    (strcmp(namelist[n]->d_name, ".") != 0)) {
				sprintf(newpath, "%s/%s", path,
					namelist[n]->d_name);
				traverse(newpath);
			}
			free(namelist[n]);
		}
		free(namelist);
	}
	return 0;
}


int process_mboxfile(char *file, u64_t userid)
{
	regex_t preg;
	int result;
	FILE *infile;
	int in_msg, header_passed;
	char newunique[UID_SIZE];
	unsigned cnt, len, newlines;
	u64_t msgid = 0, size;
	char saved;

	if ((result =
	     regcomp(&preg, mbox_delimiter_pattern, REG_NOSUB)) != 0) {
		trace(TRACE_ERROR, "Regex compilation failed.");
		return -1;
	}

	if ((infile = fopen(file, "r")) == 0) {

		trace(TRACE_ERROR, "Could not open file [%s]", infile);
		return -1;
	}

	in_msg = 0;
	cnt = 0;
	size = 0;
	newlines = 0;

	while (!feof(infile) && !ferror(infile)) {
		if (fgets(&blk[cnt], MAX_LINESIZE, infile) == 0)
			break;

		/* check if this is an mbox delimiter */
		if (regexec(&preg, &blk[cnt], 0, NULL, 0) == 0) {
			if (!in_msg)
				in_msg = 1;	/* ok start of a new msg */
			else {
				/* update & end message */
				db_insert_message_block(blk, cnt, msgid,0);

				create_unique_id(newunique, msgid);
				db_update_message(msgid, newunique,
						  size + cnt,
						  size + cnt + newlines);
				trace(TRACE_ERROR,
				      "message [%llu] inserted, [%u] bytes",
				      msgid, size + cnt);
			}

			/* start new message */
			msgid =
			    db_insert_message(userid, 0,
					      ERROR_IF_MBOX_NOT_FOUND, 0);
			header_passed = 0;
			cnt = 0;
			size = 0;
			newlines = 0;
		} else {
			newlines++;
			if (header_passed == 0) {
				/* we're still reading the header */
				len = strlen(&blk[cnt]);
				if (strcmp(&blk[cnt], "\n") == 0) {
					db_insert_message_block(blk,
								cnt + len,
								msgid,1);
					header_passed = 1;
					size += (cnt + len);
					cnt = 0;
				} else
					cnt += len;
			} else {
				/* this is body data */
				len = strlen(&blk[cnt]);
				cnt += len;

				if (cnt >= READ_BLOCK_SIZE) {
					/* write block */
					saved = blk[READ_BLOCK_SIZE];

					blk[READ_BLOCK_SIZE] = '\0';
					db_insert_message_block(blk,
								READ_BLOCK_SIZE,
								msgid,0);
					blk[READ_BLOCK_SIZE] = saved;

					memmove(blk, &blk[READ_BLOCK_SIZE],
						cnt - (READ_BLOCK_SIZE));
					size += READ_BLOCK_SIZE;
					cnt -= READ_BLOCK_SIZE;
				}
			}
		}
	}

	/* update & end message */
	if (msgid > 0) {
		db_insert_message_block(blk, cnt, msgid,0);

		create_unique_id(newunique, msgid);
		db_update_message(msgid, newunique, size + cnt,
				  size + cnt + newlines);
		trace(TRACE_ERROR, "message [%llu] inserted, [%u] bytes",
		      msgid, size + cnt);
	}

	fclose(infile);
	return 0;
}
