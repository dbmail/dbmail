/*
 * config.h
 * header file for a general configurationfile read tool
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#include "list.h"
#include "debug.h"

#define CONFIG_ERROR_LEVEL TRACE_WARNING

#define FIELDSIZE 1024
#define FIELDLEN FIELDSIZE
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


#endif
