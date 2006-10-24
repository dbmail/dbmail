/*
 *  Renamed dm_getopt because MySQL keeps putting things in my_ space.
 *
 *  getopt.h - cpp wrapper for dm_getopt to make it look like getopt.
 *  Copyright 1997, 2000, 2001, 2002, Benjamin Sittler
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#ifndef USE_DM_GETOPT
#  include <getopt.h>
#endif

#ifdef USE_DM_GETOPT

#  ifndef DM_GETOPT_H
     /* Our include guard first. */
#    define DM_GETOPT_H
     /* Try to kill the system getopt.h */
#    define _GETOPT_DECLARED
#    define _GETOPT_H
#    define GETOPT_H

#    undef getopt
#    define getopt dm_getopt
#    undef getopt_long
#    define getopt_long dm_getopt_long
#    undef getopt_long_only
#    define getopt_long_only dm_getopt_long_only
#    undef _getopt_internal
#    define _getopt_internal _dm_getopt_internal
#    undef opterr
#    define opterr dm_opterr
#    undef optind
#    define optind dm_optind
#    undef optopt
#    define optopt dm_optopt
#    undef optarg
#    define optarg dm_optarg

#    ifdef __cplusplus
extern "C" {
#    endif

/* UNIX-style short-argument parser */
extern int dm_getopt(int argc, char * argv[], const char *opts);

extern int dm_optind, dm_opterr, dm_optopt;
extern char *dm_optarg;

struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

/* human-readable values for has_arg */
#    undef no_argument
#    define no_argument 0
#    undef required_argument
#    define required_argument 1
#    undef optional_argument
#    define optional_argument 2
 
/* GNU-style long-argument parsers */
extern int dm_getopt_long(int argc, char * argv[], const char *shortopts,
                       const struct option *longopts, int *longind);
 
extern int dm_getopt_long_only(int argc, char * argv[], const char *shortopts,
                            const struct option *longopts, int *longind);
 
extern int _dm_getopt_internal(int argc, char * argv[], const char *shortopts,
                            const struct option *longopts, int *longind,
                           int long_only);

#    ifdef __cplusplus
}
#    endif

#  endif /* DM_GETOPT_H */

#endif /* USE_DM_GETOPT */
