/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _MIME_H
#define _MIME_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"

#define MEM_BLOCK 1024

struct mime_record
{
/* if these are to be changed to ptrs, the following has to be updated:
   mime.c (duh)
   db_parse_as_text
   a cleanup for all the memory allocated 
*/
  char field[MIME_FIELD_MAX];
  char value[MIME_VALUE_MAX];
};



int mime_list(char *blkdata, struct list *mimelist);
void mime_findfield(const char *fname, struct list *mimelist, struct mime_record **mr);
int mail_adr_list(char *scan_for_field, struct list *targetlist, struct list *mimelist);
int mail_adr_list_special(int offset, int max, char *address_array[], struct list *users);
int mime_readheader(char *blkdata, u64_t *blkidx, 
		    struct list *mimelist, u64_t *headersize);

#endif
