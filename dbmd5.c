/* $Id$ 
 * Functions to create md5 hash from buf */

#include <stdio.h>
#include <stdlib.h>

#include "md5.h"

char makemd5[33](char *buf)
{
	struct GdmMD5Context mycontext;
	unsigned char const mystring[]="aaaa\n";
	unsigned char result[16];
	unsigned char md5hash[33];
	int i;
	
	gdm_md5_init (&mycontext);
	gdm_md5_update (&mycontext,mystring,strlen(buf));
	gdm_md5_final (result,&mycontext);
	
	for (i = 0; i < 16; i++) {
	    sprintf (&md5hash[i*2],"%02x", result[i]);
	}
	return md5hash;
}
