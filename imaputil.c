/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * imaputil.c
 *
 * IMAP-server utility functions implementations
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include "imaputil.h"
#include "imap4.h"
#include "debug.h"
#include "sstack.h"
#include "dbmysql.h"

#ifndef MAX_LINESIZE
#define MAX_LINESIZE 1024
#endif

#define BUFLEN 2048
#define MAX_ARGS 128

extern const char AcceptedChars[];
extern const char AcceptedTagChars[];
extern const char AcceptedMailboxnameChars[];

char base64encodestring[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* returned by date_sql2imap() */
char _imapdate[IMAP_INTERNALDATE_LEN] = "03-Nov-1979 00:00:00";

const char *month_desc[]= 
{ 
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

const char *item_desc[] = 
{
  "TEXT", "HEADER", "MIME", "HEADER.FIELDS", "HEADER.FIELDS.NOT"
};

const char *envelope_items[] = 
{
  "from", "sender", "reply-to", "to", "cc", "bcc", NULL
};

/* 
 * retrieve_structure()
 *
 * retrieves the MIME-IMB structure of a message. The msg should be in the format
 * as build by db_fetch_headers().
 *
 * shows extension data if show_extension_data != 0
 *
 * returns -1 on error, 0 on success
 */
int retrieve_structure(FILE *outstream, mime_message_t *msg, int show_extension_data)
{
  struct mime_record *mr;
  struct element *curr;
  struct list *header_to_use;
  mime_message_t rfcmsg;
  char *subtype,*extension,*newline;

  fprintf(outstream,"(");

  mime_findfield("content-type", &msg->rfcheader, &mr);

  if (msg->mimeheader.start != NULL || !mr ||
      (mr && strncasecmp(mr->value,"multipart", strlen("multipart")) != 0))
    {
      /* show basic fields:
       * content-type, content-subtype, (parameter list), 
       * content-id, content-description, content-transfer-encoding,
       * size
       */
      
      if (msg->mimeheader.start == NULL)
	header_to_use = &msg->rfcheader;   /* we're dealing with a single-part RFC msg here */
      else
	header_to_use = &msg->mimeheader;  /* we're dealing with a pure-MIME header here */

      mime_findfield("content-type", header_to_use, &mr);
      if (mr && strlen(mr->value) > 0)
	show_mime_parameter_list(outstream, mr, 1, 0);
      else
	fprintf(outstream,"\"TEXT\" \"PLAIN\" (\"CHARSET\" \"US-ASCII\")"); /* default */

      mime_findfield("content-id", header_to_use, &mr);
      if (mr && strlen(mr->value) > 0)
	fprintf(outstream, " \"%s\"",mr->value);
      else
	fprintf(outstream, " NIL");

      mime_findfield("content-description", header_to_use, &mr);
      if (mr && strlen(mr->value) > 0)
	fprintf(outstream, " \"%s\"",mr->value);
      else
	fprintf(outstream, " NIL");

      mime_findfield("content-transfer-encoding", header_to_use, &mr);
      if (mr && strlen(mr->value) > 0)
	fprintf(outstream, " \"%s\"",mr->value);
      else
	fprintf(outstream, " \"7BIT\"");

      /* now output size */
      /* add msg->bodylines because \n is dumped as \r\n */
      fprintf(outstream, " %lu ", msg->bodysize + msg->bodylines ); 


      /* now check special cases, first case: message/rfc822 */
      mime_findfield("content-type", header_to_use, &mr);
      if (mr && strncasecmp(mr->value, "message/rfc822", strlen("message/rfc822")) == 0 &&
	  header_to_use != &msg->rfcheader)
	{
	  /* msg/rfc822 found; extra items to be displayed:
	   * (a) body envelope of rfc822 msg
	   * (b) body structure of rfc822 msg
	   * (c) msg size (lines)
	   */
	  
	  if (retrieve_envelope(outstream, &msg->rfcheader) == -1)
	    return -1;

	  memmove(&rfcmsg, msg, sizeof(rfcmsg));
	  rfcmsg.mimeheader.start = NULL; /* forget MIME-part */
	  
	  if (retrieve_structure(outstream, &rfcmsg, show_extension_data) == -1)
	    return -1;
	  
	  /* output # of lines */
	  fprintf(outstream, " %lu", msg->bodylines);
	}

      /* second case: text 
       * NOTE: if 'content-type' is absent, TEXT is assumed 
       */
      if ((mr && strncasecmp(mr->value, "text", strlen("text")) == 0) || !mr)
	fprintf(outstream, "%lu", msg->bodylines);      /* output # of lines */

      if (show_extension_data)
	{
	  mime_findfield("content-md5", header_to_use, &mr);
	  if (mr && strlen(mr->value) > 0)
	    fprintf(outstream, " \"%s\"",mr->value);
	  else
	    fprintf(outstream, " NIL");

	  mime_findfield("content-disposition", header_to_use, &mr);
	  if (mr && strlen(mr->value) > 0)
	    {
	      fprintf(outstream, " (");
	      show_mime_parameter_list(outstream, mr, 0, 0);
	      fprintf(outstream, ")");
	    }
	  else
	    fprintf(outstream, " NIL");

	  mime_findfield("content-language", header_to_use, &mr);
	  if (mr && strlen(mr->value) > 0)
	    fprintf(outstream, " \"%s\"",mr->value);
	  else
	    fprintf(outstream, " NIL");
	}
    }
  else
    {
      /* check for a multipart message */
      mime_findfield("content-type", &msg->rfcheader, &mr);
      if (mr && strncasecmp(mr->value,"multipart", strlen("multipart")) == 0)
	{
	  curr = list_getstart(&msg->children);
	  while (curr)
	    {
	      if (retrieve_structure(outstream, (mime_message_t*)curr->data, 
				     show_extension_data) == -1)
		return -1;

	      curr = curr->nextnode;
	    }

	  /* show multipart subtype */
	  subtype = strchr(mr->value, '/');
	  extension = strchr(subtype, ';');
	  
	  if (!subtype)
	    fprintf(outstream, " NIL");
	  else
	    {
	      if (!extension)
		{
		  newline = strchr(subtype, '\n');
		  if (!newline)
		    return -1;

		  *newline = 0;
		  fprintf(outstream, " \"%s\"", subtype+1);
		  *newline = '\n';
		}
	      else
		{
		  *extension = 0;
		  fprintf(outstream, " \"%s\"", subtype+1);
		  *extension = ';';
		}
	    }

	  /* show extension data (after subtype) */
	  if (extension && show_extension_data)
	    {
	      show_mime_parameter_list(outstream, mr, 0, 1);

	      /* FIXME: should give body-disposition & body-language here */
	      fprintf(outstream, " NIL NIL");
	    }
	}
      else
	{
	  /* ??? */
	}
    }
  fprintf(outstream,")");

  return 0;
}


/*
 * retrieve_envelope()
 *
 * retrieves the body envelope of an RFC-822 msg
 *
 * returns -1 on error, 0 on success
 */
int retrieve_envelope(FILE *outstream, struct list *rfcheader)
{
  struct mime_record *mr;
  int idx;

  fprintf(outstream,"(");

  mime_findfield("date", rfcheader, &mr);
  if (mr && strlen(mr->value) > 0)
    fprintf(outstream, " \"%s\"",mr->value);
  else
    fprintf(outstream, "NIL");

  mime_findfield("subject", rfcheader, &mr);
  if (mr && strlen(mr->value) > 0)
    fprintf(outstream, " {%d}\r\n%s",strlen(mr->value),mr->value);
  else
    fprintf(outstream, " NIL");

  /* now from, sender, reply-to, to, cc, bcc, in-reply-to fields;
   * note that multiple mailaddresses are separated by ','
   */

  for (idx=0; envelope_items[idx]; idx++)
    {
      mime_findfield(envelope_items[idx], rfcheader, &mr);
      if (mr && strlen(mr->value) > 0)
	{
	  show_address_list(outstream, mr);
	}
      else if (strcasecmp(envelope_items[idx], "reply-to") == 0)
	{
	  /* default this field */
	  mime_findfield("from", rfcheader, &mr);
	  if (mr && strlen(mr->value) > 0)
	    show_address_list(outstream, mr);
	  else /* no from field ??? */
	    fprintf(outstream, "((NIL NIL \"nobody\" \"nowhere.org\"))");
	}
      else if (strcasecmp(envelope_items[idx], "sender") == 0)
	{
	  /* default this field */
	  mime_findfield("from", rfcheader, &mr);
	  if (mr && strlen(mr->value) > 0)
	    show_address_list(outstream, mr);
	  else /* no from field ??? */
	    fprintf(outstream, "((NIL NIL \"nobody\" \"nowhere.org\"))");
	}
      else
	fprintf(outstream, " NIL");
  
    }

  mime_findfield("in-reply-to", rfcheader, &mr);
  if (mr && strlen(mr->value) > 0)
    fprintf(outstream, " \"%s\"",mr->value);
  else
    fprintf(outstream, " NIL");

  mime_findfield("message-id", rfcheader, &mr);
  if (mr && strlen(mr->value) > 0)
    fprintf(outstream, " \"%s\"",mr->value);
  else
    fprintf(outstream, " NIL");

  fprintf(outstream,") ");

  return 0;
}


/*
 * show_address_list()
 *
 * gives an address list, output to outstream
 */
int show_address_list(FILE *outstream, struct mime_record *mr)
{
  int delimiter,i,inquote,start,has_split;

  fprintf(outstream," (");
      
  /* find ',' to split up multiple addresses */
  delimiter = 0;
	  
  do
    {
      fprintf(outstream,"(");

      start = delimiter;

      for (inquote=0; mr->value[delimiter] && !(mr->value[delimiter] == ',' && !inquote); 
	   delimiter++) 
	if (mr->value[delimiter] == '\"') inquote ^= 1;

      if (mr->value[delimiter])
	mr->value[delimiter] = 0; /* replace ',' by NULL-termination */
      else
	delimiter = -1; /* this will be the last one */

      /* the address currently being processed is now contained within
       * &mr->value[start] 'till first '\0'
       */
	      
      /* possibilities for the mail address:
       * (1) name <user@domain>
       * (2) <user@domain>
       * (3) user@domain
       * scan for '<' to determine which case we should be dealing with;
       */

      for (i=start, inquote=0; mr->value[i] && !(mr->value[i] == '<' && !inquote); i++) 
	if (mr->value[i] == '\"') inquote ^= 1;

      if (mr->value[i])
	{
	  if (i > start+2)
	    {
	      /* name is contained in &mr->value[start] untill &mr->value[i-2] */
	      /* name might be quoted */
	      if (mr->value[start] == '\"')
		fprintf(outstream, "\"%.*s\"", i-start-3,&mr->value[start+1]);
	      else
		fprintf(outstream, "\"%.*s\"", i-start-1,&mr->value[start]);
	      
	    }
	  else
	    fprintf(outstream, "NIL");

	  start = i+1; /* skip to after '<' */
	}
      else
	fprintf(outstream, "NIL");

      fprintf(outstream, " NIL "); /* source route ?? smtp at-domain-list ?? */

      /* now display user domainname; &mr->value[start] is starting point */
      fprintf(outstream, "\"");

      /*
       * added a check for whitespace within the address (not good)
       */
      for (i=start, has_split=0; mr->value[i] && mr->value[i] != '>' && !isspace(mr->value[i]); 
	   i++)
	{
	  if (mr->value[i] == '@')
	    {
	      fprintf(outstream,"\" \"");
	      has_split = 1;
	    }
	  else
	    fprintf(outstream,"%c",mr->value[i]);
	}

      if (!has_split)
	fprintf(outstream,"\" \"\""); /* '@' did not occur */
      else
	fprintf(outstream, "\"");
		  
      if (delimiter > 0)
	{
	  mr->value[delimiter++] = ','; /* restore & prepare for next iteration */
	  while (isspace(mr->value[delimiter])) delimiter++;
	}

      fprintf(outstream, ")");

    } while (delimiter > 0) ;
	  
  fprintf(outstream,")");

  return 0;
}



/*
 * show_mime_parameter_list()
 *
 * shows mime name/value pairs, output to outstream
 * 
 * if force_subtype != 0 'NIL' will be outputted if no subtype is specified
 * if only_extension != 0 only extension data (after first ';') will be shown
 */
int show_mime_parameter_list(FILE *outstream, struct mime_record *mr, 
			     int force_subtype, int only_extension)
{
  int idx,delimiter,start,end;

  /* find first delimiter */
  for (delimiter = 0; mr->value[delimiter] && mr->value[delimiter] != ';'; delimiter++) ;

  if (mr->value[delimiter])
    mr->value[delimiter] = 0;
  else
    delimiter = -1;

  if (!only_extension)
    {
      /* find main type in value */
      for (idx = 0; mr->value[idx] && mr->value[idx] != '/'; idx++) ;
	  
      if (mr->value[idx] && (idx<delimiter || delimiter == -1))
	{
	  mr->value[idx] = 0;
	  fprintf(outstream,"\"%s\" \"%s\"", mr->value, &mr->value[idx+1]);
	  mr->value[idx] = '/';
	}
      else
	fprintf(outstream,"\"%s\" %s", mr->value, force_subtype ? "NIL" : "");
    }

  if (delimiter >= 0)
    {
      /* extra parameters specified */
      mr->value[delimiter] = ';';
      idx=delimiter;

      fprintf(outstream," (");

      /* extra params: <name>=<val> [; <name>=<val> [; ...etc...]]
	       * note that both name and val may or may not be enclosed by 
	       * either single or double quotation marks
	       */

      do
	{
	  /* skip whitespace */
	  for (idx++; isspace(mr->value[idx]); idx++) ;
		  
	  if (!mr->value[idx]) break; /* ?? */
		  
	  /* check if quotation marks are specified */
	  if (mr->value[idx] == '\"' || mr->value[idx] == '\'')
	    {
	      start = ++idx;
	      while (mr->value[idx] && mr->value[idx] != mr->value[start-1]) idx++;
		      
	      if (!mr->value[idx] || mr->value[idx+1] != '=') /* ?? no end quote */
		break;

	      end = idx;
	      idx+=2;        /* skip to after '=' */
	    }
	  else
	    {
	      start = idx;
	      while (mr->value[idx] && mr->value[idx] != '=') idx++;
		      
	      if (!mr->value[idx]) /* ?? no value specified */
		break;
		      
	      end = idx;
	      idx++;        /* skip to after '=' */
	    }

	  fprintf(outstream,"\"%.*s\" ", (end-start), &mr->value[start]);


	  /* now process the value; practically same procedure */

	  if (mr->value[idx] == '\"' || mr->value[idx] == '\'')
	    {
	      start = ++idx;
	      while (mr->value[idx] && mr->value[idx] != mr->value[start-1]) idx++;
		      
	      if (!mr->value[idx]) /* ?? no end quote */
		break;

	      end = idx;
	      idx++;
	    }
	  else
	    {
	      start = idx;

	      while (mr->value[idx] && !isspace(mr->value[idx]) &&
		     mr->value[idx] != ';') idx++;
		      
	      end = idx;
	    }

	  fprintf(outstream,"\"%.*s\"", (end-start), &mr->value[start]);
		  
		  /* check for more name/val pairs */
	  while (mr->value[idx] && mr->value[idx] != ';') idx++;

	  if (mr->value[idx])
	    fprintf(outstream," ");

	} while (mr->value[idx]); 

      fprintf(outstream,")");
	      
    }
  else
    {
      fprintf(outstream," NIL");
    }

  return 0;
}


/* 
 * get_part_by_num()
 *
 * retrieves a msg part by it's numeric specifier
 * 'part' is assumed to be valid! (i.e '1.2.3.44')
 * returns NULL if there is no such part 
 */
mime_message_t* get_part_by_num(mime_message_t *msg, const char *part)
{
  int nextpart,j;
  char *endptr;
  struct element *curr;

  if (part == NULL || strlen(part) == 0 || msg == NULL)
    return msg;

  nextpart = strtoul(part, &endptr, 10); /* strtoul() stops at '.' */

  for (j=1, curr=list_getstart(&msg->children); j<nextpart && curr; j++, curr = curr->nextnode);

  if (!curr)
    return NULL;

  if (*endptr)
    return get_part_by_num((mime_message_t*)curr->data, &endptr[1]); /* skip dot in part */

  return (mime_message_t*)curr->data;
}  


/*
 * rfcheader_dump()
 * 
 * dumps rfc-header fields belonging to rfcheader
 * the fields to be dumped are specified in fieldnames, an array containing nfields items
 *
 * if equal_type == 0 the field match criterium is inverted and non-matching fieldnames
 * will be selected
 *
 * to select every headerfield it suffices to set nfields and equal_type to 0
 *
 * returns number of bytes written to outstream
 */
long rfcheader_dump(FILE *outstream, struct list *rfcheader, char **fieldnames, int nfields,
		    int equal_type)
{
  struct mime_record *mr;
  struct element *curr;
  long size = 0;

  curr = list_getstart(rfcheader);
  if (rfcheader == NULL || curr == NULL)
    {
      size += fprintf(outstream, "NIL\r\n");
      return size;
    }

  curr = list_getstart(rfcheader);
  while (curr)
    {
      mr = (struct mime_record*)curr->data;

      if (haystack_find(nfields, fieldnames, mr->field) == equal_type)
	size += fprintf(outstream, "%s: %s\r\n", mr->field, mr->value);  /* ok output this field */

      curr = curr->nextnode;
    }
  size += fprintf(outstream,"\r\n");
  
  return size;
}      
  

/*
 * mimeheader_dump()
 * 
 * dumps mime-header fields belonging to mimeheader
 *
 */
long mimeheader_dump(FILE *outstream, struct list *mimeheader)
{
  struct mime_record *mr;
  struct element *curr;
  long size = 0;

  curr = list_getstart(mimeheader);
  if (mimeheader == NULL || curr == NULL)
    {
      size = fprintf(outstream, "NIL\r\n");
      return size;
    }

  while (curr)
    {
      mr = (struct mime_record*)curr->data;
      size += fprintf(outstream, "%s: %s\r\n", mr->field, mr->value);
      curr = curr->nextnode;
    }
  size += fprintf(outstream,"\r\n");

  return size;
}      


/* 
 * find a string in an array of strings
 */
int haystack_find(int haystacklen, char **haystack, const char *needle)
{
  int i;

  for (i=0; i<haystacklen; i++)
    if (strcasecmp(haystack[i], needle) == 0)
      return 1;

  return 0;
}


/*
 * next_fetch_item()
 *
 * retrieves next item to be fetched from an argument list starting at the given
 * index. The update index is returned being -1 on 'no-more' and -2 on error.
 * arglist is supposed to be formatted according to build_args_array()
 *
 */
int next_fetch_item(char **args, int idx, fetch_items_t *fi)
{
  int invalidargs,indigit,j,ispeek,shouldclose,delimpos;

  memset(fi, 0, sizeof(fetch_items_t)); /* init */
  fi->bodyfetch.itemtype = -1; /* expect no body fetches (a priori) */
  invalidargs = 0;

  if (!args[idx])
    return -1; /* no more */

  if (args[idx][0] == '(')
    idx++;

  if (!args[idx])
    return -2; /* error */

  if (strcasecmp(args[idx], "flags") == 0)
    fi->getFlags = 1;
  else if (strcasecmp(args[idx], "internaldate") == 0)
    fi->getInternalDate = 1;
  else if (strcasecmp(args[idx], "uid") == 0)
    fi->getUID = 1;
  else if (strcasecmp(args[idx], "rfc822") == 0)
    {
      fi->getRFC822 = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "rfc822.peek") == 0)
    {
      fi->getRFC822Peek = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "rfc822.header") == 0)
    {
      fi->getRFC822Header = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "rfc822.size") == 0)
    {
      fi->getSize = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "rfc822.text") == 0)
    {
      fi->getRFC822Text = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "body") == 0 || strcasecmp(args[idx],"body.peek") == 0)
    {
      fi->msgparse_needed = 1;

      if (!args[idx+1] || strcmp(args[idx+1],"[") != 0)
	{
	  if (strcasecmp(args[idx],"body.peek") == 0)
	    return -2; /* error DONE */
	  else
	    fi->getMIME_IMB_noextension = 1; /* just BODY specified */
	}
      else
	{
	  /* determine wheter or not to set the seen flag */
	  ispeek = (strcasecmp(args[idx],"body.peek") == 0);
	      
	  /* now read the argument list to body */
	  idx++; /* now pointing at '[' (not the last arg, parentheses are matched) */
	  idx++; /* now pointing at what should be the item type */

	  if (strcmp(args[idx], "]") == 0)
	    {
	      /* specified body[] or body.peek[] */
	      if (ispeek)
		fi->getBodyTotalPeek = 1;
	      else
		fi->getBodyTotal = 1;

	      return idx+1; /* DONE */
	    }
	      
	  if (ispeek)
	    fi->bodyfetch.noseen = 1;

	  /* first check if there is a partspecifier (numbers & dots) */
	  indigit = 0;
	  for (j=0; args[idx][j]; j++)
	    {
	      if (isdigit(args[idx][j]))
		{
		  indigit = 1;
		  continue;
		}
	      else if (args[idx][j] == '.')
		{
		  if (!indigit)
		    {
		      /* error, single dot specified */
		      invalidargs = 1;
		      break;
		    }
		      
		  indigit = 0;
		  continue;
		}
	      else
		break; /* other char found */
	    }
	      
	  if (invalidargs)
	    return -2; /* error DONE */

	  if (j > 0)
	    {
	      if (indigit && args[idx][j])
		return -2; /* error DONE */
		
	      /* partspecifier present, save it */
	      if (j >= IMAP_MAX_PARTSPEC_LEN)
		return -2; /* error DONE */

	      strncpy(fi->bodyfetch.partspec, args[idx], j);
	    }
	  fi->bodyfetch.partspec[j] = '\0';

	  shouldclose = 0;
	  if (strcasecmp(&args[idx][j], "text") == 0)
	    {
	      fi->bodyfetch.itemtype = BFIT_TEXT;
	      shouldclose = 1;
	    }
	  else if (strcasecmp(&args[idx][j], "header") == 0)
	    {
	      fi->bodyfetch.itemtype = BFIT_HEADER;
	      shouldclose = 1;
	    }
	  else if (strcasecmp(&args[idx][j], "mime") == 0)
	    {
	      if (j == 0)
		return -2; /* error DONE */

	      fi->bodyfetch.itemtype = BFIT_MIME;
	      shouldclose = 1;
	    }
	  else if (strcasecmp(&args[idx][j], "header.fields") == 0)
	    fi->bodyfetch.itemtype = BFIT_HEADER_FIELDS;
	  else if (strcasecmp(&args[idx][j], "header.fields.not") == 0)
	    fi->bodyfetch.itemtype = BFIT_HEADER_FIELDS_NOT;
	  else if (args[idx][j] == '\0')
	    {
	      fi->bodyfetch.itemtype = BFIT_TEXT_SILENT;
	      shouldclose = 1;
	    }
	  else
	    return -2; /* error DONE */

	  if (shouldclose)
	    {
	      if (strcmp(args[idx+1],"]") != 0)
		return -2; /* error DONE */
	    }
	  else
	    {
	      idx++; /* should be at '(' now */
	      if (strcmp(args[idx],"(") != 0)
		return -2; /* error DONE */
  
	      idx++; /* at first item of field list now, remember idx */
	      fi->bodyfetch.argstart = idx;

	      /* walk on untill list terminates (and it does 'cause parentheses are matched) */
	      while (strcmp(args[idx],")") != 0)
		idx++;

	      fi->bodyfetch.argcnt = idx - fi->bodyfetch.argstart;
		  
	      if (fi->bodyfetch.argcnt == 0 || strcmp(args[idx+1],"]") != 0)
		return -2; /* error DONE */
	    }
	      
	  idx++; /* points to ']' now */

	  /* check if octet start/cnt is specified */
	  if (args[idx+1] && args[idx+1][0] == '<')
	    {
	      idx++; /* advance */

	      /* check argument */
	      if (args[idx][strlen(args[idx]) - 1 ] != '>')
		return -2; /* error DONE */

	      delimpos = -1;
	      for (j=1; j < strlen(args[idx])-1; j++)
		{
		  if (args[idx][j] == '.')
		    {
		      if (delimpos != -1)
			{
			  invalidargs = 1;
			  break;
			}
		      delimpos = j;
		    }
		  else if (!isdigit(args[idx][j]))
		    {
		      invalidargs = 1;
		      break;
		    }
		}
	      
	      if (invalidargs || delimpos == -1 || delimpos == 1 || delimpos == (strlen(args[idx])-2) )
		return -2;  /* no delimiter found or at first/last pos OR invalid args DONE */

	      /* read the numbers */
	      args[idx][strlen(args[idx]) - 1] = '\0';
	      args[idx][delimpos] = '\0';
	      fi->bodyfetch.octetstart = atoi(&args[idx][1]);
	      fi->bodyfetch.octetcnt   = atoi(&args[idx][delimpos+1]);
			
	      /* restore argument */
	      args[idx][delimpos] = '.';
	      args[idx][strlen(args[idx]) - 1] = '>';
	    }
	  else
	    {
	      fi->bodyfetch.octetstart = -1;
	      fi->bodyfetch.octetcnt   = -1;
	    }
	  /* ok all done for body item */
	}
    }
  else if (strcasecmp(args[idx], "all") == 0)
    {
      fi->getFlags = 1;
      fi->getInternalDate = 1;
      fi->getSize = 1;
      fi->getEnvelope = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "fast") == 0)
    {
      fi->getFlags = 1;
      fi->getInternalDate = 1;
      fi->getSize = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "full") == 0)
    {
      fi->getFlags = 1;
      fi->getInternalDate = 1;
      fi->getSize = 1;
      fi->getEnvelope = 1;
      fi->getMIME_IMB = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "bodystructure") == 0)
    {
      fi->getMIME_IMB = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcasecmp(args[idx], "envelope") == 0)
    {
      fi->getEnvelope = 1;
      fi->msgparse_needed = 1;
    }
  else if (strcmp(args[idx], ")") == 0)
    {
      /* only allowed if last arg here */
      if (args[idx+1])
	return -2; /* DONE */
      else
	return -1;
    }
  else
    return -2; /* DONE */

  trace(TRACE_DEBUG,"next_fetch_item(): args[idx = %d] = %s (returning %d)\n",idx,args[idx],idx+1);
  return idx+1;
}


/*
 * give_chunks()
 *
 * splits up a string delimited by a given character into a NULL-terminated array of strings
 * does not perform any quotation-mark checks
 */
char **give_chunks(const char *str, char delimiter)
{
  int cnt,i;
  char **array,*cpy,*tmp;
  
  cpy = (char*)malloc(sizeof(char) * (strlen(str) + 1));
  if (!cpy  )
    {
      trace(TRACE_ERROR, "give_chunks(): out of memory\n");
      return NULL;
    }

  strcpy(cpy,str);
  tmp = cpy;      /* save start of cpy */

  for (i=0,cnt=0; str[i]; i++)
    if (str[i] == delimiter) 
      {
	cnt++;
	cpy[i] = '\0';
      }

  cnt++; /* add last part */

  /* alloc mem */
  cnt++; /* for NULL termination */
  array = (char**)malloc(sizeof(char*) * cnt);

  if (!array)
    {
      trace(TRACE_ERROR, "give_chunks(): out of memory\n");
      free(cpy);
      return NULL;
    }

  for (i=0,cnt=0; str[i]; i++)
    {
      if (str[i] == delimiter)
	{
	  array[cnt++] = cpy;   /* save this address */
	  cpy = &tmp[i+1];      /* let cpy point to next string */
	}
    }

  /* copy last part */
  array[cnt++] = cpy;

  array[cnt] = NULL;
  return array;
}


/* 
 * free_chunks()
 *
 * frees memory allocated using give_chunks()
 */
void free_chunks(char **chunks)
{
  if (!chunks)
    return;

  if (chunks[0])
    free(chunks[0]); /* the entire array will be freed now */

  free(chunks); /* free ptrs to strings */
}



/*
 * check_state_and_args()
 *
 * checks if the user is in the right state & the numbers of arguments;
 * a state of -1 specifies any state
 * arguments can be grouped by means of parentheses
 *
 * returns 1 on succes, 0 on failure
 */
int check_state_and_args(const char *command, const char *tag, char **args, 
			 int nargs, int state, ClientInfo *ci)
{
  int i;
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;

  /* check state */
  if (state != -1)
    {
      if (ud->state != state)
	{
	  if (!(state == IMAPCS_AUTHENTICATED && ud->state == IMAPCS_SELECTED))
	    {
	      fprintf(ci->tx,"%s BAD %s command received in invalid state\n", tag, command);
	      return 0;
	    }
	}
    }

  /* check args */
  for (i=0; i<nargs; i++)
    {
      if (!args[i])
	{
	  /* error: need more args */
	  fprintf(ci->tx,"%s BAD missing argument%s to %s\n", tag, 
		  (nargs == 1) ? "" : "(s)", command);
	  return 0;
	}
    }

  for (i=0; args[i]; i++) ;

  if (i > nargs)
    {
      /* error: too many args */
      fprintf(ci->tx,"%s BAD too many arguments to %s\n",tag, command);
      return 0;
    }

  /* succes */
  return 1;
}



/*
 * build_args_array()
 *
 * builds an dimensional array of strings containing arguments based upon 
 * a series of arguments passed as a single string.
 * normal/square parentheses have special meaning:
 * '(body [all header])' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '['
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ']'
 * [6] = ')'
 *
 * quoted strings are those enclosed by double quotation marks and returned as a single argument
 * WITHOUT the enclosing quotation marks
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */

/* local defines */
#define NORMPAR 1 
#define SQUAREPAR 2
#define NOPAR 0

char *the_args[MAX_ARGS];

char **build_args_array(const char *s)
{
  int nargs=0,inquote=0,i,quotestart=0;
  int nnorm=0,nsquare=0,paridx=0,slen=0,argstart=0;
  char parlist[MAX_LINESIZE];

  if (!s)
    return NULL;

  /* check for empty string */
  if (!(*s))
    {
      the_args[0] = NULL;
      return the_args;
    }

  /* find the arguments */
  paridx = 0;
  parlist[paridx] = NOPAR;

  inquote = 0;
  slen = strlen(s);

  for (i=0,nargs=0; i<slen && nargs < MAX_ARGS-1; i++)
    {
      /* check quotes */
      if (s[i] == '"' && ((i > 0 && s[i-1] != '\\') || i == 0))
	{
	  if (inquote)
	    {
	      /* quotation end, treat quoted string as argument */
	      if (!(the_args[nargs] = (char*)malloc(sizeof(char) * (i-quotestart)) ))
		{
		  /* out of mem */
		  while (--nargs >= 0)
		    {
		      free(the_args[nargs]);
		      the_args[nargs] = NULL;
		    }
		      
		  trace(TRACE_DEBUG, 
			"IMAPD: Not enough memory while building up argument array.");
		  return NULL;
		}
		  
	      memcpy(the_args[nargs], &s[quotestart+1], i-quotestart-1);
	      the_args[nargs][i-quotestart -1] = '\0';

	      nargs++;
	      inquote = 0;
	    }
	  else
	    {
	      inquote = 1;
	      quotestart = i;
	    }

	  continue;
	}

      if (inquote)
	continue;
	
      /* check for (, ), [ or ] in string */
      if (s[i] == '(' || s[i] == ')' || s[i] == '[' || s[i] == ']')
	{
	  /* check parenthese structure */
	  if (s[i] == ')')
	    {
	      if (paridx < 0 || parlist[paridx] != NORMPAR)
		paridx = -1;
	      else
		{
		  nnorm--;
		  paridx--;
		}
	    }
	  else if (s[i] == ']')
	    {
	      if (paridx < 0 || parlist[paridx] != SQUAREPAR)
		paridx = -1;
	      else
		{
		  paridx--;
		  nsquare--;
		}
	    }
	  else if (s[i] == '(')
	    {
	      parlist[++paridx] = NORMPAR;
	      nnorm++;
	    }
	  else /* s[i] == '[' */
	    {
	      parlist[++paridx] = SQUAREPAR;
	      nsquare++;
	    }

	  if (paridx < 0)
	    {
	      /* error in parenthesis structure */
	      while (--nargs >= 0)
		{
		  free(the_args[nargs]);
		  the_args[nargs] = NULL;
		}
	      return NULL;
	    }

	  /* add this parenthesis to the arg list and continue */
	  if (!(the_args[nargs] = (char*)malloc( sizeof(" ") )) )
	    {
	      /* out of mem */
	      while (--nargs >= 0)
		{
		  free(the_args[nargs]);
		  the_args[nargs] = NULL;
		}
		      
	      trace(TRACE_DEBUG, 
		    "IMAPD: Not enough memory while building up argument array.");
	      return NULL;
	    }
	  the_args[nargs][0] = s[i];
	  the_args[nargs][1] = '\0';

	  nargs++;
	  continue;
	}
      
      if (s[i] == ' ')
	continue;

      /* at an argument start now, walk on until next delimiter
       * and save argument 
       */
      
      for (argstart = i; i<slen && !strchr(" []()",s[i]); i++)
	if (s[i] == '"')
	  {
	    if (s[i-1] == '\\') continue;
	    else break;
	  }
      
      if (!(the_args[nargs] = (char*)malloc(sizeof(char) * (i-argstart +1)) ))
	{
	  /* out of mem */
	  while (--nargs >= 0)
	    {
	      free(the_args[nargs]);
	      the_args[nargs] = NULL;
	    }
		      
	  trace(TRACE_DEBUG, 
		"IMAPD: Not enough memory while building up argument array.");
	  return NULL;
	}
		  
      memcpy(the_args[nargs], &s[argstart], i-argstart);
      the_args[nargs][i-argstart] = '\0';

      nargs++;
      i--; /* walked one too far */
    }

  if (paridx != 0)
    {
      /* error in parenthesis structure */
      while (--nargs >= 0)
	{
	  free(the_args[nargs]);
	  the_args[nargs] = NULL;
	}
      return NULL;
    }

  the_args[nargs] = NULL; /* terminate */

  /* dump args (debug) */
  for (i=0; the_args[i]; i++)
    {
      trace(TRACE_MESSAGE, "arg[%d]: '%s'\n",i,the_args[i]);
    }

  return the_args;
}
#undef NOPAR
#undef NORMPAR
#undef RIGHTPAR


/*
 * clarify_data()
 *
 * replaces all multiple spaces by a single one except for quoted spaces;
 * removes leading and trailing spaces and a single trailing newline (if present)
 */
void clarify_data(char *str)
{
  int startidx,i,inquote,endidx;


  /* remove leading spaces */
  for (i=0; str[i] == ' '; i++) ;
  memmove(str, &str[i], sizeof(char) * (strlen(&str[i])+1)); /* add one for \0 */

  /* remove CR/LF */
  endidx = strlen(str)-1;
  if (endidx >= 0 && (str[endidx] == '\n' ||str[endidx] == '\r'))
    endidx--;

  if (endidx >= 0 && (str[endidx] == '\n' ||str[endidx] == '\r'))
    endidx--;


  if (endidx == 0)
    {
      /* only 1 char left and it is not a space */
      str[1] = '\0';
      return;
    }

  /* remove trailing spaces */
  for (i=endidx; i>0 && str[i] == ' '; i--) ;
  if (i == 0)
    {
      /* empty string remains */
      *str = '\0';
      return;
    }

  str[i+1] = '\0';

  /* scan for multiple spaces */
  inquote = 0;
  for (i=0; i < strlen(str); i++)
    {
      if (str[i] == '"')
	{
	  if ((i > 0 && str[i-1]!='\\') || i == 0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	    }
	}

      if (str[i] == ' ' && !inquote)
	{
	  for (startidx = i; str[i] == ' '; i++);

	  if (i-startidx > 1)
	    {
	      /* multiple non-quoted spaces found --> remove 'm */
	      memmove(&str[startidx+1], &str[i], sizeof(char) * (strlen(&str[i])+1));
	      /* update i */
	      i = startidx+1;
	    }
	}
    }
}      
     

/*
 * is_textplain()
 *
 * checks if content-type is text/plain
 */
int is_textplain(struct list *hdr)
{
  struct mime_record *mr;
  int i,len;

  if (!hdr)
    return 0;

  mime_findfield("content-type", hdr, &mr);

  if (!mr)
    return 0;

  len = strlen(mr->value);
  for (i=0; len-i >= sizeof("text/plain"); i++)
    if (strncasecmp(&mr->value[i], "text/plain", sizeof("text/plain")-1) == 0)
      return 1;
  
  return 0;
}


/*
 * convert a mySQL date (yyyy-mm-dd hh:mm:ss) to a valid IMAP internal date:
 *                       0123456789012345678
 * dd-mon-yyyy hh:mm:ss with mon characters (i.e. 'Apr' for april)
 * 01234567890123456789
 * return value is valid until next function call.
 * NOTE: sqldate is not tested for validity. Behaviour is undefined for non-sql
 * dates.
 */
char *date_sql2imap(char *sqldate)
{
  int mon;

  /* copy day */
  _imapdate[0] = sqldate[8];
  _imapdate[1] = sqldate[9];

  /* find out which month */
  mon = strtoul(&sqldate[5], NULL, 10) - 1;
  if (mon < 0 || mon > 11)
    mon = 0;

  /* copy month */
  _imapdate[3] = month_desc[mon][0];
  _imapdate[4] = month_desc[mon][1];
  _imapdate[5] = month_desc[mon][2];

  /* copy year */
  _imapdate[7] = sqldate[0];
  _imapdate[8] = sqldate[1];
  _imapdate[9] = sqldate[2];
  _imapdate[10] = sqldate[3];

  /* copy hour */
  _imapdate[12] = sqldate[11];
  _imapdate[13] = sqldate[12];

  /* copy minutes */
  _imapdate[15] = sqldate[14];
  _imapdate[16] = sqldate[15];

  /* copy secs */
  _imapdate[18] = sqldate[17];
  _imapdate[19] = sqldate[18];

  return _imapdate;
}  


/*
 *
 */
int stridx(const char *s, char ch)
{
  int i;

  for (i=0; s[i] && s[i] != ch;  i++) ;

  return i;
}


/*
 * checkchars()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkchars(const char *s)
{
  int i;

  for (i=0; s[i]; i++)
    {
      if (stridx(AcceptedChars, s[i]) == strlen(AcceptedChars))
	{
	  /* wrong char found */
	  return 0;
	}
    }
  return 1;
}


/*
 * checktag()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checktag(const char *s)
{
  int i;

  for (i=0; s[i]; i++)
    {
      if (stridx(AcceptedTagChars, s[i]) == strlen(AcceptedTagChars))
	{
	  /* wrong char found */
	  return 0;
	}
    }
  return 1;
}


/*
 * checkmailboxname()
 *
 * performs a check to see if the mailboxname is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkmailboxname(const char *s)
{
  int i;

  if (strlen(s) == 0)
    return 0; /* empty name is not valid */

  if (strlen(s) >= IMAP_MAX_MAILBOX_NAMELEN)
    return 0; /* a too large string is not valid */

  /* check for invalid characters */
  for (i=0; s[i]; i++)
    {
      if (stridx(AcceptedMailboxnameChars, s[i]) == strlen(AcceptedMailboxnameChars))
	{
	  /* wrong char found */
	  return 0;
	}
    }

  /* check for double '/' */
  for (i=1; s[i]; i++)
    {
      if (s[i] == '/' && s[i-1] == '/')
	return 0;
    }

  /* check if the name consists of a single '/' */
  if (strlen(s) == 1 && s[0] == '/')
    return 0;

  return 1;
}

  

/*
 * base64encode()
 *
 * encodes a string using base64 encoding
 */
void base64encode(char *in,char *out)
{
  for ( ; strlen(in) >= 3; in+=3)
    {
      *out++ = base64encodestring[ (in[0] & 0xFC) >> 2U];
      *out++ = base64encodestring[ ((in[0] & 0x03) << 4U) | ((in[1] & 0xF0) >> 4U) ];
      *out++ = base64encodestring[ ((in[1] & 0x0F) << 2U) | ((in[2] & 0xC0) >> 6U) ];
      *out++ = base64encodestring[ (in[2] & 0x3F) ];
    }

  if (strlen(in) == 2)
    {
      /* 16 bits left to encode */
      *out++ = base64encodestring[ (in[0] & 0xFC) >> 2U];
      *out++ = base64encodestring[ ((in[0] & 0x03) << 4U) | ((in[1] & 0xF0) >> 4U) ];
      *out++ = base64encodestring[ ((in[1] & 0x0F) << 2U) ];
      *out++ = '=';

      return;
    }
      
  if (strlen(in) == 1)
    {
      /* 8 bits left to encode */
      *out++ = base64encodestring[ (in[0] & 0xFC) >> 2U];
      *out++ = base64encodestring[ ((in[0] & 0x03) << 4U) ];
      *out++ = '=';
      *out++ = '=';

      return;
    }
}      


/*
 * base64decode()
 *
 * decodes a base64 encoded string
 */
void base64decode(char *in,char *out)
{
  for ( ; strlen(in) >= 4; in+=4)
    {
      *out++ = (stridx(base64encodestring, in[0]) << 2U) 
	| ((stridx(base64encodestring, in[1]) & 0x30) >> 4U);

      *out++ = ((stridx(base64encodestring, in[1]) & 0x0F) << 4U) 
	| ((stridx(base64encodestring, in[2]) & 0x3C) >> 2U);

      *out++ = ((stridx(base64encodestring, in[2]) & 0x03) << 6U) 
	| (stridx(base64encodestring, in[3]) & 0x3F);
    }

  *out = 0;
}      


/*
 * binary_search()
 *
 * performs a binary search on array to find key
 * array should be ascending in values
 *
 * returns index of key in array or -1 if not found
 */
int binary_search(const unsigned long *array, int arraysize, unsigned long key)
{
  int low,high,mid;

  low = 0;
  high = arraysize-1;

  while (low <= high)
    {
      mid = (high+low)/2;
      if (array[mid] < key)
	low = mid+1;
      else if (array[mid] > key)
	high = mid-1;
      else
	return mid;
    }

  return -1; /* not found */
}

