/* $Id$ 
	Headers for bounce.c */

#include <stdio.h>
#include <stdlib.h>
#include "debug.h"
#include "list.h"
#include "mime.h"

#define SENDMAIL "/usr/sbin/sendmail"

#define BOUNCE_NO_SUCH_USER 1
#define BOUNCE_STORAGE_LIMIT_REACHED 2
#define BOUNCE INVALID_MAIL_FORMAT 3
