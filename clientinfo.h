/* 
 * clientinfo.h
 *
 * definition of clientinfo structure & initializing function prototypes
 */

#ifndef CLIENT_INFO_H
#define CLIENT_INFO_H

#include <stdio.h>
#include "config.h"

#define IPNUM_LEN 32

typedef struct
{
  FILE *tx,*rx;
  char ip[IPNUM_LEN];       /* client IP-number */
  field_t clientname;       /* resolved client ip */
  char *timeoutMsg;
  int  timeout;             /* server timeout (seconds) */
  void *userData;
} clientinfo_t;


typedef clientinfo_t ClientInfo; /* FIXME alias shouldn't be necessary */

#endif
