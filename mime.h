/* $Id$ */

#ifndef MIME_H_
#define MIME_H_
#include "dbmysql.h"
#include "debug.h"
#include "list.h"

int mail_adr_list(char *scan_for_field, struct list *targetlist);
#endif
