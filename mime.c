/* $Id$
 * Functions for parsing a mime mailheader (actually just for scanning for email messages
	and parsing the messageID */

#include "config.h"
#include "mime.h"

#define MIME_FIELD_MAX 128
#define MIME_VALUE_MAX 1024
#define MEM_BLOCK 1024

struct mime_record
{
	char field[MIME_FIELD_MAX];
	char value[MIME_VALUE_MAX];
};

extern struct list mimelist;
extern struct list users;

void mime_list(char *blkdata, unsigned long blksize)
/* returns <0 on failure */
/* created a mimelist and maillist as positive result */
{
	int t,x,i=0; /* counter */
	
	int valid_mime_lines=0;
	
	char *tmpstr, *tmpfield, *tmpvalue, *ptr;
	struct mime_record *mr;
	struct element *el;   
	
	trace (TRACE_INFO, "mime_list(): entering mime loop");
	
	memtst((tmpstr=(char *)malloc(MEM_BLOCK))==NULL);
	memtst((tmpfield=(char *)malloc(MIME_FIELD_MAX))==NULL);
	memtst((tmpvalue=(char *)malloc(MIME_VALUE_MAX))==NULL);
	memtst((mr=(struct mime_record *)malloc(sizeof(struct mime_record)))==NULL);

	while (i<blksize)
	{
	/* quick hack to jump over those naughty \n\t fields */
	ptr=blkdata;
	for (t=0;ptr!=NULL;t++)
		{
		if ((ptr[0]=='\n') && (ptr[1]!='\t'))
			break;
		ptr++;
		}

	x=strlen(blkdata)-strlen(ptr); /* first line */
	if (x>1)
		{
		memtst((strncpy(tmpstr,blkdata,x))==NULL);
		memtst((strcpy (&tmpstr[x],"\0"))==NULL);
		if (ptr!=NULL)
			{
			blkdata=ptr+1;
			trace (TRACE_DEBUG,"mime_list(): captured array [%s]",tmpstr); 

			/* parsing tmpstring for field and data */
			/* field is xxxx: */

			ptr=strstr(tmpstr,":");
			if (ptr!=NULL)
				{
				valid_mime_lines++;
				memtst((strncpy(tmpfield,tmpstr,ptr-tmpstr))==NULL); 
				tmpfield[ptr-tmpstr]='\0';

				/* skip all spaces and semicollons after the fieldname */
				while ((*ptr==':') || (*ptr==' ')) ptr++;
				memtst((strcpy (tmpvalue,ptr))==NULL); 
				tmpvalue[strlen(ptr)]='\0';

				memtst((strcpy (mr->field,tmpfield))==NULL);
				memtst((strcpy (mr->value,tmpvalue))==NULL);

				trace (TRACE_DEBUG,"mime_list(): mimepair found: [%s] [%s] \n",mr->field, mr->value); 

				memtst((el=list_nodeadd(&mimelist,mr,sizeof (*mr)))==NULL);

				i=i+x;
				}
			}
		}
		else break;
	}
	trace (TRACE_DEBUG,"mime_list(): mimeloop finished");
	if (valid_mime_lines<2)
		{
		free(blkdata);
		trace (TRACE_STOP,"mime_list(): no valid mime headers found");
		}
}

int mail_adr_list_special(int offset, int max, char *address_array[]) 
	{
	int mycount;

	trace (TRACE_INFO,"mail_adr_list_special(): gathering info from command line");
	for (mycount=offset;mycount!=max; mycount++)
		{
		trace(TRACE_DEBUG,"mail_adr_list_special(): adding [%s] to userlist",address_array[mycount]);
		memtst((list_nodeadd(&users,address_array[mycount],(strlen(address_array[mycount])+1)))==NULL);
		}
   return mycount;
	}
  
int mail_adr_list()
{
	struct element *raw;
	struct mime_record *mr;
	char *tmpvalue, *ptr,*tmp;

	
	memtst((tmpvalue=(char *)calloc(MIME_VALUE_MAX,sizeof(char)))==NULL);

	trace (TRACE_INFO,"mail_adr_list(): mail address parser starting");

	while ((raw=list_getstart(&mimelist))!=NULL)
	{
	  mr=(struct mime_record *)raw->data;
			/* FIXME: need to check which header we
			 * need to scan. IMHO it's only the delivered 
			 * to */
   	  if ((strcasecmp(mr->field,"delivered-to")==0))
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
				memtst((list_nodeadd(&users,tmpvalue,
								(strlen(tmpvalue)+1)))==NULL);

				/* printf ("total nodes:\n");
					list_showlist(&users);
					next address */
				ptr=strstr(ptr,"@");
				trace (TRACE_DEBUG,"mail_adr_list(): found %s, next in list is %s",
						tmpvalue,ptr);
				}
			}
			list_nodedel(&mimelist,raw->data);
	}

	trace (TRACE_DEBUG,"mail_adr_list(): found %d emailaddresses",list_totalnodes(&users));
	
	trace (TRACE_INFO,"mail_adr_list(): mail address parser finished");

	if (list_totalnodes(&users)==0) /* no addresses found */
		return -1;
	return 0;
}
