/* $Id$ 
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * Functions to create md5 hash from buf */

#include <stdio.h>
#include <stdlib.h>

#include "dbmd5.h"
#include "md5.h"
#include "debug.h"

unsigned char *makemd5(char *buf)
{
	struct GdmMD5Context mycontext;
	unsigned char result[16];
	unsigned char *md5hash;
	int i;
	
	md5hash=(unsigned char *)my_malloc(33);
	
	gdm_md5_init (&mycontext);
	gdm_md5_update (&mycontext,buf,strlen(buf));
	gdm_md5_final (result,&mycontext);
	
	for (i = 0; i < 16; i++) {
	    sprintf (&md5hash[i*2],"%02x", result[i]);
	}

	printf ("hash found [%s]\n",md5hash);
	
	return md5hash;
}
