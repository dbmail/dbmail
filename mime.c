/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * Functions for parsing a mime mailheader (actually just for scanning for email messages
	and parsing the messageID */

#include "config.h"
#include "mime.h"
#include <ctype.h>

/* extern char *header; */
/* extern unsigned long headersize; */

/* extern struct list mimelist;  */
/* extern struct list users; */


/* 
 * mime_list()
 *
 * build a list of MIME header items
 * blkdata should be a NULL-terminated array
 *
 * returns -1 on failure, 0 on success
 */
int mime_list(char *blkdata, struct list *mimelist)
{
  int valid_mime_lines=0,idx;
	
  char *endptr, *startptr, *delimiter;
  struct mime_record *mr;
  struct element *el;   
	
  trace (TRACE_INFO, "mime_list(): entering mime loop\n");

  list_init(mimelist);
  /* alloc mem */
#ifdef USE_EXIT_ON_ERROR
  memtst((mr=(struct mime_record *)malloc(sizeof(struct mime_record)))==NULL);
#else
  mr=(struct mime_record *)malloc(sizeof(struct mime_record));

  if (!mr)
    {
      trace(TRACE_ERROR, "mime_list(): out of memory\n");
      return -1;
    }
#endif

  startptr = blkdata;
  while (*startptr)
    {
      /* quick hack to jump over those naughty \n\t fields */
      endptr = startptr;
      while (*endptr)
	{
	  if (endptr[0]=='\n' && endptr[1]!='\t')
	    {
	      if (endptr != blkdata && *(endptr-1) == ';')
		{
		  endptr++;
		  continue;
		}
	      else
		{
		  break;
		}
	    }
	  endptr++;
	}

      if (!(*endptr))
	{
	  /* end of data block reached */
	  free(mr);
	  return 0;
	}

      /* endptr points to linebreak now */
      /* MIME field+value is string from startptr till endptr */

      *endptr = '\0'; /* replace newline to terminate string */

      trace(TRACE_DEBUG,"mime_list(): captured array [%s]\n",startptr); 

      /* parsing tmpstring for field and data */
      /* field is name:value */

      delimiter = strchr(startptr,':');

      if (delimiter)
	{
	  /* found ':' */
	  valid_mime_lines++;
	  *delimiter = '\0'; /* split up strings */

	  /* skip all spaces and colons after the fieldname */
	  idx = 1;
	  while ((delimiter[idx]==':') || (delimiter[idx]==' ')) idx++;

	  /* &delimiter[idx] is field value, startptr is field name */
	  strncpy(mr->field, startptr, MIME_FIELD_MAX);
	  strncpy(mr->value, &delimiter[idx], MIME_VALUE_MAX);

	  trace (TRACE_DEBUG,"mime_list(): mimepair found: [%s] [%s] \n",mr->field, mr->value); 

#ifdef USE_EXIT_ON_ERROR
	  memtst((el=list_nodeadd(mimelist,mr,sizeof (*mr)))==NULL);
#else
	  el = list_nodeadd(mimelist,mr,sizeof (*mr));
	  if (!el)
	    {
	      trace(TRACE_ERROR, "mime_list(): cannot add element to list\n");
	      free(mr);
	      return -1;
	    }
#endif
	  /* restore blkdata */
	  *delimiter = ':';
	  *endptr = '\n';
	  startptr = endptr+1; /* advance to next field */

	  if (*startptr == '\n')
	    {
	      /* end of header: double newline */
	      free(mr);
	      return 0;
	    }
	}
      else 
	{
	  /* no field/value delimiter found, non-valid MIME-header */
	  free(mr);
	  trace(TRACE_ERROR,"Non valid mimeheader found, freeing list...\n");
	  list_freelist(&mimelist->start);
	  mimelist->total_nodes = 0;
	  trace(TRACE_ERROR,"freeing list done, start: %X\n ",mimelist->start);

	  return -1;
	}
    }

  free(mr); /* no longer need this */

  trace(TRACE_DEBUG,"mime_list(): mimeloop finished\n");
  if (valid_mime_lines < 2)
    {
#ifdef USE_EXIT_ON_ERROR
      free(blkdata);
      trace(TRACE_STOP,"mime_list(): no valid mime headers found");
#else
      trace(TRACE_ERROR,"mime_list(): no valid mime headers found\n");
      return -1;
#endif
    }

  /* success */
  trace(TRACE_DEBUG," *** mime_list() done ***\n");
  return 0;
}



/* 
 * mime_readheader()
 *
 * same as mime_list() but adds the number of bytes read to blkidx
 * and returns the number of newlines passed
 *
 * headersize will be set to the actual amount of bytes used to store the header:
 * field/value strlen()'s plus 4 bytes for each headeritem: ': ' (field/value
 * separator) and '\r\n' to end the line.
 *
 * newlines within value will be expanded to '\r\n'
 *
 * if blkdata[0] == \n no header is expected and the function will return immediately
 * (headersize 0)
 *
 * returns -1 on parse failure, -2 on memory error; number of newlines on succes
 */
int mime_readheader(char *blkdata, unsigned long *blkidx, struct list *mimelist, unsigned long *headersize)
{
  int valid_mime_lines=0,idx,totallines=0,j;
  unsigned fieldlen,vallen,prevlen,new_add=1;
/*  unsigned long saved_idx = *blkidx; only needed if we bail out on invalid data */
	
  char *endptr, *startptr, *delimiter;
  struct mime_record *mr,*prev_mr=NULL;
  struct element *el = NULL;   
	
  trace (TRACE_DEBUG, "mime_readheader(): entering mime loop\n");

  list_init(mimelist);
  *headersize = 0;

  if (blkdata[0] == '\n')
    {
      trace(TRACE_DEBUG,"mime_readheader(): found an empty header\n");
      (*blkidx)++; /* skip \n */
      return 1; /* found 1 newline */
    }

  /* alloc mem */
  mr=(struct mime_record *)malloc(sizeof(struct mime_record));

  if (!mr)
    {
      trace(TRACE_ERROR, "mime_readheader(): out of memory\n");
      return -2;
    }

  startptr = blkdata;
  while (*startptr)
    {
      /* quick hack to jump over those naughty \n\t fields */
      endptr = startptr;
      while (*endptr)
	{
	  /* field-ending: \n + (non-white space) */
	  if (*endptr == '\n') 
	    {
	      totallines++;
	      if (!isspace(endptr[1]) || endptr[1] == '\n')
		break;
	    }
	  
	  endptr++;
	}

      if (!(*endptr))
	{
	  /* end of data block reached (??) */
	  free(mr);
	  *blkidx += (endptr-startptr);

	  return totallines;
	}

      /* endptr points to linebreak now */
      /* MIME field+value is string from startptr till endptr */

      *endptr = '\0'; /* replace newline to terminate string */


      /* parsing tmpstring for field and data */
      /* field is name:value */

      delimiter = strchr(startptr,':');

      if (delimiter)
	{
	  /* found ':' */
	  valid_mime_lines++;
	  *delimiter = '\0'; /* split up strings */

	  /* skip all spaces and colons after the fieldname */
	  idx = 1;
	  while ((delimiter[idx]==':') || (delimiter[idx]==' ')) idx++;

	  /* &delimiter[idx] is field value, startptr is field name */
	  fieldlen = snprintf(mr->field, MIME_FIELD_MAX, "%s", startptr);
	  for (vallen=0,j=0; delimiter[idx+j] && vallen < MIME_VALUE_MAX; j++,vallen++)
	    {
	      if (delimiter[idx+j] == '\n')
		{
		  mr->value[vallen++] = '\r';
		  /* dont count newline here: it is already counted */
		}
	      
	      mr->value[vallen] = delimiter[idx+j];
	    }
	  
	  if (vallen < MIME_VALUE_MAX)
	    mr->value[vallen] = 0;
	  else
	    mr->value[MIME_VALUE_MAX-1] = 0;
	  
	  /* snprintf returns -1 if max is readched (libc <= 2.0.6) or the strlen (libc >= 2.1)
	   * check the value. it does not count the \0.
	   */

	  if (fieldlen < 0 || fieldlen >= MIME_FIELD_MAX)
	    *headersize += MIME_FIELD_MAX;
	  else
	    *headersize += fieldlen;

	  if (vallen < 0 || vallen >= MIME_VALUE_MAX)
	    *headersize += MIME_VALUE_MAX;
	  else
	    *headersize += vallen;

	  *headersize += 4; /* <field>: <value>\r\n --> four more */


/*	  strncpy(mr->field, startptr, MIME_FIELD_MAX);
	  strncpy(mr->value, &delimiter[idx], MIME_VALUE_MAX);
*/
/*	  trace (TRACE_DEBUG,"mime_readheader(): mimepair found: [%s] [%s] \n",mr->field, mr->value); 
*/
	  el = list_nodeadd(mimelist,mr,sizeof (*mr));
	  if (!el)
	    {
	      trace(TRACE_ERROR, "mime_readheader(): cannot add element to list\n");
	      free(mr);
	      return -2;
	    }

	  /* restore blkdata */
	  *delimiter = ':';
	}
      else
	{
	  /* 
	   * ok invalid mime header, what now ? 
	   * just add it with an empty field name EXCEPT
	   * when the previous stored field value ends on a ';'
	   * in this case probably someone forget to place a \t on the next line
	   * then we will try to add it to the previous element
	   */
	   
	  new_add = 1;
	  if (el)
	    {
	      prev_mr = (struct mime_record*)(el->data);
	      prevlen = strlen(prev_mr->value);
	      
	      new_add = (prev_mr->value[prevlen-1] == ';') ? 0 : 1;
	    }
	  
	  if (new_add)
	    {
	      /* add a new field with no name */
	      strcpy(mr->field, "");
	      vallen = snprintf(mr->value, MIME_VALUE_MAX, "%s", startptr);

	      if (vallen < 0 || vallen >= MIME_VALUE_MAX)
		*headersize += MIME_VALUE_MAX;
	      else
		*headersize += vallen;
	      
	      *headersize += 4; /* <field>: <value>\r\n --> four more */
	      
	      el = list_nodeadd(mimelist,mr,sizeof (*mr));

	      if (!el)
		{
		  trace(TRACE_ERROR, "mime_readheader(): cannot add element to list\n");
		  free(mr);
		  return -2;
		}
	    }
	  else
	    {
	      /* try to add the value to the previous one */
	      if (prevlen < MIME_VALUE_MAX - (strlen(startptr) + 4))
		{
		  prev_mr->value[prevlen] = '\n';
		  prev_mr->value[prevlen+1] = '\t';

		  strcpy(&prev_mr->value[prevlen+2], startptr);

		  *headersize += (strlen(startptr) + 2); 
		}
	      else
		{
		  trace(TRACE_WARNING,"mime_readheader(): failed adding data (length would exceed "
			"MIME_VALUE_MAX [currently %d])\n",MIME_VALUE_MAX);
		}
	    }
	}

      *endptr = '\n'; /* restore blkdata */

      *blkidx += (endptr-startptr);
      (*blkidx)++;

      startptr = endptr+1; /* advance to next field */
      
      if (*startptr == '\n')
	{
	  /* end of header: double newline */
	  totallines++;
	  (*blkidx)++;
	  (*headersize)+=2;
	  trace(TRACE_DEBUG,"mime_readheader(): found double newline; header size: %d lines\n",
		totallines);
	  free(mr);
	  return totallines;
	}

    }

  /* everything down here should be unreachable */

  free(mr); /* no longer need this */

  trace(TRACE_DEBUG,"mime_readheader(): mimeloop finished\n");
  if (valid_mime_lines < 2)
    {
      trace(TRACE_ERROR,"mime_readheader(): no valid mime headers found\n");
      return -1;
    }

  /* success ? */
  trace(TRACE_DEBUG," *** mime_readheader() done ***\n");
  return totallines;
}



/*
 * mime_findfield()
 *
 * finds a MIME header field
 *
 */
void mime_findfield(const char *fname, struct list *mimelist, struct mime_record **mr)
{
  struct element *current;

  current = list_getstart(mimelist);
  while (current)
    {
      *mr = current->data;   /* get field/value */
      
      if (strncasecmp((*mr)->field, fname, strlen(fname)) == 0)
	return; /* found */

      current = current->nextnode;
    }

  *mr = NULL;
}
      
  

int mail_adr_list_special(int offset, int max, char *address_array[], struct list *users) 
{
  int mycount;

  trace (TRACE_INFO,"mail_adr_list_special(): gathering info from command line");
  for (mycount=offset;mycount!=max; mycount++)
    {
      trace(TRACE_DEBUG,"mail_adr_list_special(): adding [%s] to userlist",address_array[mycount]);
      memtst((list_nodeadd(users,address_array[mycount],(strlen(address_array[mycount])+1)))==NULL);
    }
  return mycount;
}

  
int mail_adr_list(char *scan_for_field, struct list *targetlist, struct list *mimelist,
		  struct list *users, char *header, unsigned long headersize)
{
  struct element *raw;
  struct mime_record *mr;
  char *tmpvalue, *ptr,*tmp;

  trace (TRACE_DEBUG,"mail_adr_list(): mimelist currently has [%d] nodes",mimelist->total_nodes);
  if (mimelist->total_nodes==0)
    {
      /* we need to parse the header first 
	 this is because we're in SPECIAL_DELIVERY mode so
	 normally we wouldn't need any scanning */
      trace (TRACE_INFO,"mail_adr_list(): parsing mimeheader from message");
      mime_list(header,mimelist);
    }
  
  memtst((tmpvalue=(char *)calloc(MIME_VALUE_MAX,sizeof(char)))==NULL);

  trace (TRACE_INFO,"mail_adr_list(): mail address parser starting");

  raw=list_getstart(mimelist);
  trace (TRACE_DEBUG,"mail_adr_list(): total fields in header %lu",mimelist->total_nodes);
  while (raw!=NULL)
    {
      mr=(struct mime_record *)raw->data;
      trace (TRACE_DEBUG,"mail_adr_list(): scanning for %s",scan_for_field);
      if ((strcasecmp(mr->field, scan_for_field)==0))
	{
	  /* Scan for email addresses and add them to our list */
	  /* the idea is to first find the first @ and go both ways */
	  /* until an non-emailaddress character is found */
	  ptr=strstr(mr->value,"@");
	  while (ptr!=NULL)
	    {
				/* found an @! */
				/* first go as far left as possible */
	      tmp=ptr;
	      while ((tmp!=mr->value) && 
		     (tmp[0]!='<') && 
		     (tmp[0]!=' ') && 
		     (tmp[0]!='\0') && 
		     (tmp[0]!=','))
		tmp--;
	      if ((tmp[0]=='<') || (tmp[0]==' ') || (tmp[0]=='\0')
		  || (tmp[0]==',')) tmp++;
	      while ((ptr!=NULL) &&
		     (ptr[0]!='>') && 
		     (ptr[0]!=' ') && 
		     (ptr[0]!=',') &&
		     (ptr[0]!='\0'))  
		ptr++;
	      memtst((strncpy(tmpvalue,tmp,ptr-tmp))==NULL);
				/* always set last value to \0 to end string */
	      tmpvalue[ptr-tmp]='\0';

				/* one extra for \0 in strlen */
	      memtst((list_nodeadd(targetlist,tmpvalue,
				   (strlen(tmpvalue)+1)))==NULL);

				/* printf ("total nodes:\n");
				   list_showlist(&users);
				   next address */
	      ptr=strstr(ptr,"@");
	      trace (TRACE_DEBUG,"mail_adr_list(): found %s, next in list is %s",
		     tmpvalue,ptr);
	    }
	}
      raw=raw->nextnode;
    }

  free(tmpvalue);

  trace (TRACE_DEBUG,"mail_adr_list(): found %d emailaddresses",list_totalnodes(targetlist));
	
  trace (TRACE_INFO,"mail_adr_list(): mail address parser finished");

  if (list_totalnodes(users)==0) /* no addresses found */
    return -1;
  return 0;
}
