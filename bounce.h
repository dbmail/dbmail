/* $Id$ 
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * Headers for bounce.c */

#ifndef BOUNCE_H_
#define BOUNCE_H_

#include <stdio.h>
#include <stdlib.h>
#include "debug.h"

#define SENDMAIL "/usr/sbin/sendmail"
#define DBMAIL_FROM_ADDRESS "dbmail-bounce@greyskull.fastxs.net"
#define POSTMASTER "eelco@eelco.com"

#define BOUNCE_NO_SUCH_USER 1
#define BOUNCE_STORAGE_LIMIT_REACHED 2
#define BOUNCE_INVALID_MAIL_FORMAT 3

int bounce (char *header, char *destination_address, int type);

#endif
