# aclocal.m4 generated automatically by aclocal 1.6.3 -*- Autoconf -*-

# Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002
# Free Software Foundation, Inc.
# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY, to the extent permitted by law; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE.

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
AC_ARG_WITH(mysql,
            [  --with-mysql            use MySQL as database. Uses mysql_config
	       		  for finding includes and libraries],
            mysqlheadername="$withval")
AC_ARG_WITH(pgsql,
	    [  --with-pgsql            use PostgreSQL as database. 
                          Uses pg_config for finding includes and libraries],
            pgsqlheadername="$withval")

WARN=0
# Make sure we only select one, mysql or pgsql
if test "${mysqlheadername-x}" = "x"
then
  if test "${pgsqlheadername-x}" = "x"
  then
    NEITHER=1
    mysqlheadername=""
#    MYSQLINC=""
#    PGSQLINC=""
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
#      MYSQLINC=""
#      PGSQLINC=""
  fi
fi
if test "$WARN" = 1
  then
     AC_MSG_ERROR([

     You cannot specify both --with-mysql and --with-pgsql on the same
     build...
])
fi
])

dnl DBMAIL_CHECK_SQL_LIBS
dnl
AC_DEFUN(DBMAIL_CHECK_SQL_LIBS, [dnl
#Look for include files and libs needed to link
#use the configuration utilities (mysql_config and pg_config for this)
# MySQL first
if test ! "${mysqlheadername-x}" = "x"
then
    AC_PATH_PROG(mysqlconfig,mysql_config)
    if test [ -z "$mysqlconfig" ]
    then
        AC_MSG_ERROR([mysql_config executable not found. Make sure mysql_config is in your path])
    else
	AC_MSG_CHECKING([MySQL headers])
	MYSQLINC=`${mysqlconfig} --cflags`
	AC_MSG_RESULT([$MYSQLINC])	
        AC_MSG_CHECKING([MySQL libraries])
        SQLLIB=`${mysqlconfig} --libs`
        SQLALIB="mysql/libmysqldbmail.a"
        AC_MSG_RESULT([$SQLLIB])
   fi
else
  if test ! "${pgsqlheadername-x}" = "x"
  then
    AC_PATH_PROG(pgsqlconfig,pg_config)
    if test [ -z "$pgsqlconfig" ]
    then
        AC_MSG_ERROR([pg_config executable not found. Make sure pg_config is in your path])
    else
	AC_MSG_CHECKING([PostgreSQL headers])
	PGINCDIR=`${pgsqlconfig} --includedir`
	PGSQLINC="-I$PGINCDIR"
	AC_MSG_RESULT([$PGSQLINC])
        AC_MSG_CHECKING([PostgreSQL libraries])
        PGLIBDIR=`${pgsqlconfig} --libdir`
        SQLLIB="-L$PGLIBDIR -lpq"
        SQLALIB="pgsql/libpgsqldbmail.a"
        AC_MSG_RESULT([$SQLLIB])
    fi
  fi
fi
])
	
dnl DBMAIL_AUTH_CONF
dnl check for ldap or sql authentication
AC_DEFUN(DBMAIL_AUTH_CONF, [dnl
AC_MSG_NOTICE([checking for authentication configuration])
AC_ARG_WITH(auth-ldap,[  --with-auth-ldap=PATH	  full path to ldap header directory],
	authldapheadername="$withval$")

WARN=0
if test ! "${authldapheadername-x}" = "x"
then
  # --with-auth-ldap was specified
  AC_MSG_NOTICE([using LDAP authentication])
  if test "$withval" != "yes"
  then
    AC_MSG_CHECKING([for ldap.h (user supplied)])
    if test -r "$authldapheadername/ldap.h"
      then
      # found
        AC_MSG_RESULT([$authldapheadername/ldap.h])
        LDAPINC=$authldapheadername
      else 
      # Not found
        AC_MSG_RESULT([not found])
        LDAPINC=""
        authldapheadername=""
        AC_MSG_ERROR([
  Unable to find ldap.h where you specified, try just --with-auth-ldap to 
  have configure guess])
    fi
  else
    # Lets look in our standard paths
    AC_MSG_CHECKING([for ldap.h])
    for ldappaths in $ldapheaderpaths
    do
      if test -r "$ldappaths/ldap.h"
      then
        LDAPINC="$ldappaths"
        AC_MSG_RESULT([$ldappaths/ldap.h])
        break
      fi
    done
    if test -z "$LDAPINC"
    then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([
  Unable to locate ldap.h, try specifying with --with-ldap])
    fi
  fi
else
  AC_MSG_NOTICE([using SQL authentication])
fi
])

dnl DBMAIL_CHECK_LDAP_LIBS
dnl
AC_DEFUN(DBMAIL_CHECK_LDAP_LIBS, [dnl
# Look for libs needed to link to LDAP first
if test ! "${authldapheadername-x}" = "x"
then
  AC_CHECK_LIB(ldap,ldap_bind,[ LDAPLIB="-lldap"], [LDAPLIB=""])
  if test -z "$LDAPLIB"
  then
    AC_MSG_ERROR([
  Unable to link against ldap.  It appears you are missing the
  development libraries or they aren't in your linker's path
  ])
  fi
else
  #no ldap needed
  LDAPLIB=""
fi
])
dnl AC_COMPILE_WARNINGS
dnl set to compile with '-W -Wall'
AC_DEFUN([AC_COMPILE_WARNINGS],
[AC_MSG_CHECKING(maximum warning verbosity option)
if test -n "$CXX"
then
  if test "$GXX" = "yes"
  then
    ac_compile_warnings_opt='-Wall'
  fi
  CXXFLAGS="$CXXFLAGS $ac_compile_warnings_opt"
  ac_compile_warnings_msg="$ac_compile_warnings_opt for C++"
fi
if test -n "$CC"
then
  if test "$GCC" = "yes"
  then
    ac_compile_warnings_opt='-W -Wall -Wpointer-arith -Wstrict-prototypes -O2'
  fi
  CFLAGS="$CFLAGS $ac_compile_warnings_opt"
  ac_compile_warnings_msg="$ac_compile_warnings_msg $ac_compile_warnings_opt for C"
fi
AC_MSG_RESULT($ac_compile_warnings_msg)
unset ac_compile_warnings_msg
unset ac_compile_warnings_opt
])

							

# Like AC_CONFIG_HEADER, but automatically create stamp file. -*- Autoconf -*-

# Copyright 1996, 1997, 2000, 2001 Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
# 02111-1307, USA.

AC_PREREQ([2.52])

# serial 6

# When config.status generates a header, we must update the stamp-h file.
# This file resides in the same directory as the config header
# that is generated.  We must strip everything past the first ":",
# and everything past the last "/".

# _AM_DIRNAME(PATH)
# -----------------
# Like AS_DIRNAME, only do it during macro expansion
AC_DEFUN([_AM_DIRNAME],
       [m4_if(regexp([$1], [^.*[^/]//*[^/][^/]*/*$]), -1,
	      m4_if(regexp([$1], [^//\([^/]\|$\)]), -1,
		    m4_if(regexp([$1], [^/.*]), -1,
			  [.],
			  patsubst([$1], [^\(/\).*], [\1])),
		    patsubst([$1], [^\(//\)\([^/].*\|$\)], [\1])),
	      patsubst([$1], [^\(.*[^/]\)//*[^/][^/]*/*$], [\1]))[]dnl
])# _AM_DIRNAME


# The stamp files are numbered to have different names.
# We could number them on a directory basis, but that's additional
# complications, let's have a unique counter.
m4_define([_AM_STAMP_Count], [0])


# _AM_STAMP(HEADER)
# -----------------
# The name of the stamp file for HEADER.
AC_DEFUN([_AM_STAMP],
[m4_define([_AM_STAMP_Count], m4_incr(_AM_STAMP_Count))dnl
AS_ESCAPE(_AM_DIRNAME(patsubst([$1],
                               [:.*])))/stamp-h[]_AM_STAMP_Count])


# _AM_CONFIG_HEADER(HEADER[:SOURCES], COMMANDS, INIT-COMMANDS)
# ------------------------------------------------------------
# We used to try to get a real timestamp in stamp-h.  But the fear is that
# that will cause unnecessary cvs conflicts.
AC_DEFUN([_AM_CONFIG_HEADER],
[# Add the stamp file to the list of files AC keeps track of,
# along with our hook.
AC_CONFIG_HEADERS([$1],
                  [# update the timestamp
echo 'timestamp for $1' >"_AM_STAMP([$1])"
$2],
                  [$3])
])# _AM_CONFIG_HEADER


# AM_CONFIG_HEADER(HEADER[:SOURCES]..., COMMANDS, INIT-COMMANDS)
# --------------------------------------------------------------
AC_DEFUN([AM_CONFIG_HEADER],
[AC_FOREACH([_AM_File], [$1], [_AM_CONFIG_HEADER(_AM_File, [$2], [$3])])
])# AM_CONFIG_HEADER

