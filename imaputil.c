/* 
 * imaputil.c
 *
 * IMAP-server utility functions implementations
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "imaputil.h"
#include "imap4.h"
#include "debug.h"

extern const char AcceptedChars[];
extern const char AcceptedTagChars[];

char base64encodestring[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
 * Parentheses have special meaning:
 * '(body (all header))' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '('
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ')'
 * [6] = ')'
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */

char **build_args_array(const char *s)
{
  char **args;
  char *scpy;
  int nargs=0,inquote=0,i,quotestart,currarg;
  
  if (!s)
    return NULL;

  /* check for empty string */
  if (!(*s))
    {
      args = (char**)malloc(sizeof(char*));
      if (!args)
	return NULL;

      args[0] = NULL;
      return args;
    }

  scpy = (char*)malloc(sizeof(char)*strlen(s));
  if (!scpy)
    return NULL;

  /* copy original to scpy */
  strcpy(scpy,s);

  /* now replace all delimiters by \0 */
  for (i=0,inquote=0; i<strlen(s); i++)
    {
      if (scpy[i] == '"')
	{
	  if ((i>0 && scpy[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	    }
	}

      if ((scpy[i] == ' ' || scpy[i] == '(' || scpy[i] == ')') && !inquote)
	{
	  scpy[i] = '\0';
	}
    }

  /* count the arguments */
  for (i=0,nargs=0; i<strlen(s); i++)
    {
      if (!scpy[i])
	{
	  /* check for ( or ) in original string */
	  if (s[i] == '(' || s[i] == ')')
	    nargs++;
	      
	  continue;
	}

      if (scpy[i] == '"')
	{
	  if ((i>0 && scpy[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	      if (inquote)
		nargs++;
	    }
	}
      else
	{
	  if (!inquote)
	    {
	      /* at an argument now, proceed to end (before next NULL char) */
	      while (scpy[i] && i<strlen(s)) i++;
	      i--;
	      nargs++;
	    }
	}
    }

  /* alloc memory */
  args = (char**)malloc((nargs+1) * sizeof(char *));
  if (!args)
    {
      /* out of mem */
      free(scpy);
      return NULL;
    }

  /* single out the arguments */
  currarg = 0;
  for (i=0; i<strlen(s); i++)
    {
      if (!scpy[i])
	{
	  /* check for ( or ) in original string */
	  if (s[i] == '(' || s[i] == ')')
	    {
	      /* add parenthesis */
	      /* alloc mem */
	      args[currarg] = (char*)malloc(sizeof(char) * 2);

	      if (!args[currarg])
		{
		  /* out of mem */
		  /* free currently allocated mem */
		  for (i=0; i<currarg; i++)
		    free(args[i]);
	      
		  free(args);
		  free(scpy);
		  return NULL;
		}

	      args[currarg][0] = s[i];
	      args[currarg][1] = '\0';
	      currarg++;
	    }
	  continue;
	}

      if (scpy[i] == '"')
	{
	  if ((i>0 && s[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	      if (inquote)
		{
		  /* just started the quotation, remember idx */
		  quotestart = i;
		}
	      else
		{
		  /* alloc mem */
		  args[currarg] = (char*)malloc(sizeof(char) * (i-quotestart+1+1));
		  if (!args[currarg])
		    {
		      /* out of mem */
		      /* free currently allocated mem */
		      for (i=0; i<currarg; i++)
			free(args[i]);

		      free(args);
		      free(scpy);
		      return NULL;
		    }

		  /* copy quoted string */
		  memcpy(args[currarg], &s[quotestart], sizeof(char)*(i-quotestart+1));
		  args[currarg][i-quotestart+1] = '\0'; 
		  currarg++;
		}
	    }
	}
      else if (!inquote)
	{
	  /* at an argument now, save & proceed to end (before next NULL char) */
	  /* alloc mem */
	  args[currarg] = (char*)malloc(sizeof(char) * (strlen(&scpy[i])+1) );
	  if (!args[currarg])
	    {
	      /* out of mem */
	      /* free currently allocated mem */
	      for (i=0; i<currarg; i++)
		free(args[i]);
	      
	      free(args);
	      free(scpy);
	      return NULL;
	    }

	  /* copy arg */
	  memcpy(args[currarg], &scpy[i], sizeof(char)*(strlen(&scpy[i])+1) );
	  currarg++;

	  while (scpy[i] && i<strlen(s)) i++;
	  i--;
	}
    }

  free(scpy);

  args[currarg] = NULL; /* terminate array */

  /* dump args (debug) */
  for (i=0; args[i]; i++)
    {
      trace(TRACE_MESSAGE, "arg[%d]: '%s'\n",i,args[i]);
    }

  return args;
}


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
 * retourneert de idx v/h eerste voorkomen van ch in s,
 * of strlen(s) als ch niet voorkomt
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
