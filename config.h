/* $Id$ */

#ifndef  _CONFIG_H
#define  _CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <syslog.h>

/* DATABASE SPECIFIC */
#define HOST "10.0.0.100"
#define USER "root"
#define PASS ""
#define MAILDATABASE "dbmail"

#define UID_SIZE 70


void mime_list(char *blkdata,unsigned long blksize);
char *read_header(unsigned long *blksize);
int db_connect();
int mail_adr_list();
int mail_adr_list_dlv(char *local_rec);
void check_duplicates();
int insert_messages();
#endif
