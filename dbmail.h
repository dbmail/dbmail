/*
 * dbmail.h
 * header file for a general configuration
 */

#ifndef _DBMAIL_H
#define _DBMAIL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
#define VERSION "1.2"
#define PACKAGE "dbmail"
#endif

#include "list.h"
#include "debug.h"

#define CONFIG_ERROR_LEVEL TRACE_WARNING

#define FIELDSIZE 1024
#define FIELDLEN FIELDSIZE
#define COPYRIGHT "(c) 1999-2003 IC&S, The Netherlands"
#define DEFAULT_CONFIG_FILE "/etc/dbmail.conf"

/* uncomment this if you want informative process titles */
//#define PROC_TITLES

typedef char field_t[FIELDSIZE];
 
typedef struct
{
  field_t name, value;
} item_t;

int ReadConfig(const char *serviceName, const char *cfilename, struct list *items);
int GetConfigValue(const field_t name, struct list *items, field_t value);

/* some common used functions reading config options */
void GetDBParams(field_t host, field_t db, field_t user, field_t pass, struct list *cfg);
void SetTraceLevel(struct list *cfg);

#define STATUS_NEW	0
#define STATUS_SEEN	1
#define STATUS_DELETE	2
#define STATUS_PURGE	3
#define STATUS_UNUSED	4
#define STATUS_INSERT	5
#define STATUS_ERROR	6

#endif
