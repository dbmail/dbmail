dnl aclocal.m4 generated automatically by aclocal 1.4-p4

dnl Copyright (C) 1994, 1995-8, 1999 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY, to the extent permitted by law; without
dnl even the implied warranty of MERCHANTABILITY or FITNESS FOR A
dnl PARTICULAR PURPOSE.

# Do all the work for Automake.  This macro actually does too much --
# some checks are only needed if your package does certain things.
# But this isn't really a big deal.

# serial 1

dnl Usage:
dnl AM_INIT_AUTOMAKE(package,version, [no-define])

AC_DEFUN([AM_INIT_AUTOMAKE],
[AC_REQUIRE([AC_PROG_INSTALL])
PACKAGE=[$1]
AC_SUBST(PACKAGE)
VERSION=[$2]
AC_SUBST(VERSION)
dnl test to see if srcdir already configured
if test "`cd $srcdir && pwd`" != "`pwd`" && test -f $srcdir/config.status; then
  AC_MSG_ERROR([source directory already configured; run "make distclean" there first])
fi
ifelse([$3],,
AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE", [Name of package])
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Version number of package]))
AC_REQUIRE([AM_SANITY_CHECK])
AC_REQUIRE([AC_ARG_PROGRAM])
dnl FIXME This is truly gross.
missing_dir=`cd $ac_aux_dir && pwd`
AM_MISSING_PROG(ACLOCAL, aclocal, $missing_dir)
AM_MISSING_PROG(AUTOCONF, autoconf, $missing_dir)
AM_MISSING_PROG(AUTOMAKE, automake, $missing_dir)
AM_MISSING_PROG(AUTOHEADER, autoheader, $missing_dir)
AM_MISSING_PROG(MAKEINFO, makeinfo, $missing_dir)
AC_REQUIRE([AC_PROG_MAKE_SET])])

#
# Check to make sure that the build environment is sane.
#

AC_DEFUN([AM_SANITY_CHECK],
[AC_MSG_CHECKING([whether build environment is sane])
# Just in case
sleep 1
echo timestamp > conftestfile
# Do `set' in a subshell so we don't clobber the current shell's
# arguments.  Must try -L first in case configure is actually a
# symlink; some systems play weird games with the mod time of symlinks
# (eg FreeBSD returns the mod time of the symlink's containing
# directory).
if (
   set X `ls -Lt $srcdir/configure conftestfile 2> /dev/null`
   if test "[$]*" = "X"; then
      # -L didn't work.
      set X `ls -t $srcdir/configure conftestfile`
   fi
   if test "[$]*" != "X $srcdir/configure conftestfile" \
      && test "[$]*" != "X conftestfile $srcdir/configure"; then

      # If neither matched, then we have a broken ls.  This can happen
      # if, for instance, CONFIG_SHELL is bash and it inherits a
      # broken ls alias from the environment.  This has actually
      # happened.  Such a system could not be considered "sane".
      AC_MSG_ERROR([ls -t appears to fail.  Make sure there is not a broken
alias in your environment])
   fi

   test "[$]2" = conftestfile
   )
then
   # Ok.
   :
else
   AC_MSG_ERROR([newly created file is older than distributed files!
Check your system clock])
fi
rm -f conftest*
AC_MSG_RESULT(yes)])

dnl AM_MISSING_PROG(NAME, PROGRAM, DIRECTORY)
dnl The program must properly implement --version.
AC_DEFUN([AM_MISSING_PROG],
[AC_MSG_CHECKING(for working $2)
# Run test in a subshell; some versions of sh will print an error if
# an executable is not found, even if stderr is redirected.
# Redirect stdin to placate older versions of autoconf.  Sigh.
if ($2 --version) < /dev/null > /dev/null 2>&1; then
   $1=$2
   AC_MSG_RESULT(found)
else
   $1="$3/missing $2"
   AC_MSG_RESULT(missing)
fi
AC_SUBST($1)])

# Add --enable-maintainer-mode option to configure.
# From Jim Meyering

# serial 1

AC_DEFUN([AM_MAINTAINER_MODE],
[AC_MSG_CHECKING([whether to enable maintainer-specific portions of Makefiles])
  dnl maintainer-mode is disabled by default
  AC_ARG_ENABLE(maintainer-mode,
[  --enable-maintainer-mode enable make rules and dependencies not useful
                          (and sometimes confusing) to the casual installer],
      USE_MAINTAINER_MODE=$enableval,
      USE_MAINTAINER_MODE=no)
  AC_MSG_RESULT($USE_MAINTAINER_MODE)
  AM_CONDITIONAL(MAINTAINER_MODE, test $USE_MAINTAINER_MODE = yes)
  MAINT=$MAINTAINER_MODE_TRUE
  AC_SUBST(MAINT)dnl
]
)

# Define a conditional.

AC_DEFUN([AM_CONDITIONAL],
[AC_SUBST($1_TRUE)
AC_SUBST($1_FALSE)
if $2; then
  $1_TRUE=
  $1_FALSE='#'
else
  $1_TRUE='#'
  $1_FALSE=
fi])

dnl  DBMAIL_MSG_CONFIGURE_START()
dnl
AC_DEFUN(DBMAIL_MSG_CONFIGURE_START, [dnl
AC_MSG_RESULT([
This is dbmail's GNU configure script.
It's going to run a bunch of strange tests to hopefully
make your compile work without much twiddling.
])
])

dnl DBMAIL_BOTH_SQL_CHECK
dnl
AC_DEFUN(DBMAIL_BOTH_SQL_CHECK, [dnl
AC_ARG_WITH(mysql,[  --with-mysql=PATH       full path to mysql header directory],
            mysqlheadername="$withval")
AC_ARG_WITH(pgsql,[  --with-pgsql=PATH       full path to pgsql header directory],
            pgsqlheadername="$withval")

WARN=0
# Make sure we only select one, mysql or pgsql
if test "${mysqlheadername-x}" = "x"
then
  if test "${pgsqlheadername-x}" = "x"
  then
    NEITHER=1
    mysqlheadername=""
    MYSQLINC=""
    PGSQLINC=""
  fi
fi
if test "$NEITHER" = 1
  then
     AC_MSG_ERROR([

     You have to specify --with-mysql or --with-pgsql to build.
])
fi



if test ! "${mysqlheadername-x}" = "x"
then
  if test ! "${pgsqlheadername-x}" = "x"
    then
      WARN=1
      mysqlheadername=""
      MYSQLINC=""
      PGSQLINC=""
  fi
fi
if test "$WARN" = 1
  then
     AC_MSG_ERROR([

     You cannot specify both --with-mysql and --with-pgsql on the same
     build...
])
fi

# If mysql is specified, lets check if they specified a place too first
if test ! "${mysqlheadername-x}" = "x"
then
  # --with-mysql was specified
  if test "$withval" != "yes"
  then
    AC_MSG_CHECKING([for mysql.h (user supplied)])
    if test -r "$mysqlheadername/mysql.h"
      then
      # found
        AC_MSG_RESULT([$mysqlheadername/mysql.h])
        MYSQLINC=$mysqlheadername
      else 
      # Not found
        AC_MSG_RESULT([not found])
        MYSQLINC=""
        mysqlheadername=""
        AC_MSG_ERROR([
  Unable to find mysql.h where you specified, try just --with-mysql to 
  have configure guess])
    fi
  else
    # Lets look in our standard paths
    AC_MSG_CHECKING([for mysql.h])
    for mysqlpaths in $mysqlheaderpaths
    do
      if test -r "$mysqlpaths/mysql.h"
      then
        MYSQLINC="$mysqlpaths"
        AC_MSG_RESULT([$mysqlpaths/mysql.h])
        break
      fi
    done
    if test -z "$MYSQLINC"
    then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([
  Unable to locate mysql.h, try specifying with --with-mysql])
    fi
  fi
fi


# If postgres is specified, lets check if they specified a place too first
if test ! "${pgsqlheadername-x}" = "x"
then
  # --with-pgsql was specified
  if test "$withval" != "yes"
  then
    AC_MSG_CHECKING([for libpq-fe.h (user supplied)])
    if test -r "$pgsqlheadername/libpq-fe.h"
      then
      # found
        AC_MSG_RESULT([$pgsqlheadername/libpq-fe.h])
        PGSQLINC=$pgsqlheadername
      else 
      # Not found
        AC_MSG_RESULT([not found])
        PGSQLINC=""
        pgsqlheadername=""
        AC_MSG_ERROR([
  Unable to find libpq-fe.h where you specified, try just --with-pgsql to 
  have configure guess])
    fi
  else
    # Lets look in our standard paths
    AC_MSG_CHECKING([for libpq-fe.h])
    for pgsqlpaths in $pgsqlheaderpaths
    do
      if test -r "$pgsqlpaths/libpq-fe.h"
      then
        PGSQLINC="$pgsqlpaths"
        AC_MSG_RESULT([$pgsqlpaths/libpq-fe.h])
        break
      fi
    done
    if test -z "$PGSQLINC"
    then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([
  Unable to locate libpq-fe.h, try specifying with --with-pgsql])
    fi
  fi
fi
])

dnl DBMAIL_CHECK_SQL_LIBS
dnl
AC_DEFUN(DBMAIL_CHECK_SQL_LIBS, [dnl
#Look for libs needed to link
# MySQL first
if test ! "${mysqlheadername-x}" = "x"
then
  AC_CHECK_LIB(mysqlclient,mysql_real_connect,[ SQLLIB="-lmysqlclient" SQLALIB="mysql/libmysqldbmail.a"], [SQLLIB="" SQLALIB=""])
  if test -z "$SQLLIB"
  then
    AC_MSG_ERROR([
  Unable to link against mysqlclient.  It appears you are missing the
  development libraries or they aren't in your linker's path
])
  fi
else
  if test ! "${pgsqlheadername-x}" = "x"
  then
    AC_CHECK_LIB(pq, PQconnectdb, [ SQLLIB="-lpq" SQLALIB="pgsql/libpgsqldbmail.a"], [SQLLIB="" SQLALIB=""])
    if test -z "$SQLLIB"
    then
      AC_MSG_ERROR([
  Unable to link against pq.  It appears you are missing the development
  libraries or they aren't in your linker's path
])
    fi
  fi
fi
])

# Like AC_CONFIG_HEADER, but automatically create stamp file.

AC_DEFUN(AM_CONFIG_HEADER,
[AC_PREREQ([2.12])
AC_CONFIG_HEADER([$1])
dnl When config.status generates a header, we must update the stamp-h file.
dnl This file resides in the same directory as the config header
dnl that is generated.  We must strip everything past the first ":",
dnl and everything past the last "/".
AC_OUTPUT_COMMANDS(changequote(<<,>>)dnl
ifelse(patsubst(<<$1>>, <<[^ ]>>, <<>>), <<>>,
<<test -z "<<$>>CONFIG_HEADERS" || echo timestamp > patsubst(<<$1>>, <<^\([^:]*/\)?.*>>, <<\1>>)stamp-h<<>>dnl>>,
<<am_indx=1
for am_file in <<$1>>; do
  case " <<$>>CONFIG_HEADERS " in
  *" <<$>>am_file "*<<)>>
    echo timestamp > `echo <<$>>am_file | sed -e 's%:.*%%' -e 's%[^/]*$%%'`stamp-h$am_indx
    ;;
  esac
  am_indx=`expr "<<$>>am_indx" + 1`
done<<>>dnl>>)
changequote([,]))])

