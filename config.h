/* $Id$ */

#ifndef  _CONFIG_H
#define  _CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <syslog.h>

#define USE_DEVELOPMENT

/* DATABASE SPECIFIC */
#define HOST "localhost"
#define USER "root"
#define PASS ""

#ifdef USE_DEVELOPMENT
#define MAILDATABASE "dbmail_dev"
#else
#define MAILDATABASE "dbmail"
#endif

#define UID_SIZE 70


void mime_list(char *blkdata,unsigned long blksize);
char *read_header(unsigned long *blksize);
int db_connect();
int mail_adr_list();
int mail_adr_list_dlv(char *local_rec);
void check_duplicates();
int insert_messages();
#endif
