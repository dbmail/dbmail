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
#define HOST "localhost"
#define USER "root"
#define PASS ""

/* #define USE_DEVELOPMENT */
#ifdef USE_DEVELOPMENT
#define MAILDATABASE "dbmail_dev"
#else
#define MAILDATABASE "dbmail"
#endif

#define UID_SIZE 70


#endif
