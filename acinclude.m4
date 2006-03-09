
dnl  DBMAIL_MSG_CONFIGURE_START()
dnl
AC_DEFUN([DBMAIL_MSG_CONFIGURE_START], [dnl
prefix="/usr/local"
localstatedir="/var/run"
sysconfdir="/etc"
AC_MSG_RESULT([
This is dbmail's GNU configure script.
It's going to run a bunch of strange tests to hopefully
make your compile work without much twiddling.
])
])

dnl DBMAIL_MSG_CONFIGURE_RESULTS()
dnl
AC_DEFUN([DBMAIL_MSG_CONFIGURE_RESULTS], [dnl
AC_MSG_RESULT([
PREFIX:        $prefix
SYSCONFDIR:    $sysconfdir
LOCALSTATEDIR: $localstatedir
GLIB:          $ac_glib_libs
GMIME:         $ac_gmime_libs
MYSQL:         $MYSQLLIB
PGSQL:         $PGSQLLIB
SQLITE:        $SQLITELIB
SIEVE:         $SORTLIB
LDAP:          $LDAPLIB
SHARED:        $enable_shared
STATIC:        $enable_static
CHECK:         $with_check
])
])

dnl DBMAIL_CHECK_SHARED_OR_STATIC
dnl
AC_DEFUN([DBMAIL_SET_SHARED_OR_STATIC], [dnl
# Make sure that we've got either static or shared, not both.
if test "x$enable_shared" = xyes && test "x$enable_static" = xyes
then
     AC_MSG_ERROR([

     You cannot enable both shared and static build.
     Please choose only one to enable.
])
fi
if test "x$enable_shared" = xno && test "x$enable_static" = xno
then
  enable_shared="yes"
fi
# if test "x$enable_shared" = xyes
# then
#   CFLAGS="$CFLAGS -DSHARED"
# elif test "x$enable_static" = xyes
# then
#   CFLAGS="$CFLAGS -DSTATIC"
# fi
])


dnl DBMAIL_BOTH_SQL_CHECK
dnl
AC_DEFUN([DBMAIL_BOTH_SQL_CHECK], [dnl
AC_ARG_WITH(mysql,
            [  --with-mysql            use MySQL as database. Uses mysql_config
	       		  for finding includes and libraries],
            mysqlheadername="$withval")
AC_ARG_WITH(pgsql,
	    [  --with-pgsql            use PostgreSQL as database. 
                          Uses pg_config for finding includes and libraries],
            pgsqlheadername="$withval")
AC_ARG_WITH(sqlite,
	    [  --with-sqlite           use SQLite as database. 
                          Uses pkg-config for finding includes and libraries],
            sqliteheadername="$withval")

WARN=0
# Make sure we only select one of mysql, pgsql or sqlite
if test "${mysqlheadername-x}" = "x"
then
  if test "${pgsqlheadername-x}" = "x"
  then
    if test "${sqliteheadername-x}" = "x"
    then
      NEITHER=1
      mysqlheadername=""
    fi
  fi
fi
if test "$NEITHER" = 1
  then
     AC_MSG_ERROR([

     You have to specify --with-mysql, --with-pgsql or --with-sqlite to build.
])
fi
])

dnl DBMAIL_CHECK_SQL_LIBS
dnl
AC_DEFUN([DBMAIL_CHECK_SQL_LIBS], [dnl
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
        MYSQLLIB=`${mysqlconfig} --libs`
        MYSQLALIB="modules/.libs/libmysql.a"
	MYSQLLTLIB="modules/libmysql.la"
        AC_MSG_RESULT([$MYSQLLIB])
    fi
fi   

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
        PGSQLLIB="-L$PGLIBDIR -lpq"
        PGSQLALIB="modules/.libs/libpgsql.a"
	PGSQLLTLIB="modules/libpgsql.la"
        AC_MSG_RESULT([$PGSQLLIB])
    fi
fi

if test ! "${sqliteheadername-x}" = "x"
  then
    AC_PATH_PROG(sqliteconfig,pkg-config)
    if test [ -z "$sqliteconfig" ]
    then
        AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
    else
	AC_MSG_CHECKING([SQLite headers])
	SQLITEINC=`${sqliteconfig} --cflags sqlite --errors-to-stdout`
	if test $? != 0
	then
		SQLITEINC=`${sqliteconfig} --cflags sqlite3 --errors-to-stdout`
	fi
	dnl Neither sqlite nor sqlite3 were found. Print the error from pkg-config.
	if test $? != 0
	then
        	AC_MSG_ERROR([$SQLITEINC])
	fi
	AC_MSG_RESULT([$SQLITEINC])
        AC_MSG_CHECKING([SQLite libraries])
        SQLITELIB=`${sqliteconfig} --libs sqlite --errors-to-stdout`
	if test $? != 0
	then
        	SQLITELIB=`${sqliteconfig} --libs sqlite3 --errors-to-stdout`
	fi
	dnl Neither sqlite nor sqlite3 were found. Print the error from pkg-config.
	if test $? != 0
	then
        	AC_MSG_ERROR([$SQLITEINC])
	fi
        SQLITEALIB="modules/.libs/libsqlite.a"
	SQLITELTLIB="modules/libsqlite.la"
        AC_MSG_RESULT([$SQLITELIB])
    fi
fi
])
dnl DBMAIL_SIEVE_CONF
dnl check for ldap or sql authentication
AC_DEFUN([DBMAIL_SIEVE_CONF], [dnl
AC_MSG_RESULT([checking for sorting configuration])
AC_ARG_WITH(sieve,[  --with-sieve=PATH	  full path to libSieve header directory (don't use, not stable)],
	sieveheadername="$withval$")
dnl This always needs to be defined
SORTALIB="modules/.libs/libsort_null.a"
SORTLTLIB="modules/libsort_null.la"

WARN=0
if test ! "${sieveheadername-x}" = "x"
then
  # --with-sieve was specified
  AC_MSG_RESULT([using Sieve sorting])
  CFLAGS="$CFLAGS -DSIEVE"
  # Redefine if there's actually Sieve sorting
  SORTALIB="modules/.libs/libsort_sieve.a"
  SORTLTLIB="modules/libsort_sieve.la"
  if test "$withval" != "yes"
  then
    AC_MSG_CHECKING([for sieve2.h (user supplied)])
    if test -r "$sieveheadername/sieve2.h"
      then
      # found
        AC_MSG_RESULT([$sieveheadername/sieve2.h])
        SIEVEINC="-I$sieveheadername"
      else 
      # Not found
        AC_MSG_RESULT([not found])
        SIEVEINC=""
        sieveheadername=""
        AC_MSG_ERROR([
  Unable to find sieve2.h where you specified, try just --with-sieve to 
  have configure guess])
    fi
  else
    # Lets look in our standard paths
    AC_MSG_CHECKING([for sieve2.h])
    for sievepaths in $sieveheaderpaths
    do
      if test -r "$sievepaths/sieve2.h"
      then
        SIEVEINC="-I$sievepaths"
        AC_MSG_RESULT([$sievepaths/sieve2.h])
        break
      fi
    done
    if test -z "$SIEVEINC"
    then
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([
  Unable to locate sieve2.h, try specifying with --with-sieve])
    fi
  fi
else
  AC_MSG_RESULT([not using any sorting])
fi
])

dnl DBMAIL_CHECK_SIEVE_LIBS
dnl
AC_DEFUN([DBMAIL_CHECK_SIEVE_LIBS], [dnl
# Look for libs needed to link to SIEVE first
if test ! "${sieveheadername-x}" = "x"
then
  AC_CHECK_LIB(sieve,sieve2_listextensions,[ SORTLIB="-lsieve"], [SORTLIB=""])
  if test -z "$SORTLIB"
  then
    AC_MSG_ERROR([
  Unable to link against libSieve.  It appears you are missing the
  development libraries or they aren't in your linker's path
  ])
  fi
else
  #no Sieve needed
  SORTLIB=""
fi
])
	
dnl DBMAIL_AUTH_CONF
dnl check for ldap or sql authentication
AC_DEFUN([DBMAIL_AUTH_CONF], [dnl
AC_MSG_RESULT([checking for authentication configuration])
AC_ARG_WITH(auth-ldap,[  --with-auth-ldap=PATH	  full path to ldap header directory],
	authldapheadername="$withval")
dnl This always needs to be defined
AUTHALIB="modules/.libs/libauth_ldap.a"
AUTHLTLIB="modules/libauth_ldap.la"

WARN=0
if test ! "${authldapheadername-x}" = "x"
then
	# --with-auth-ldap was specified
	AC_MSG_RESULT([using LDAP authentication])
	CFLAGS="$CFLAGS -DAUTHLDAP"
	if test "$withval" != "yes"
	then
		AC_MSG_CHECKING([for ldap.h (user supplied)])
		if test -r "$authldapheadername/ldap.h"
		then
			# found
			AC_MSG_RESULT([$authldapheadername/ldap.h])
			LDAPINC="-I$authldapheadername"
		else 
			# Not found
			AC_MSG_RESULT([not found])
			LDAPINC=""
			authldapheadername=""
			AC_MSG_ERROR([Unable to find ldap.h where you specified, try just --with-auth-ldap to have configure guess])
		fi
	else
		# Lets look in our standard paths
		AC_MSG_CHECKING([for ldap.h])
		for ldappath in $ldapheaderpaths
		do
			if test -r "$ldappath/ldap.h"
			then
				LDAPINC="-I$ldappath"
				AC_MSG_RESULT([$ldappath/ldap.h])
				break
			fi
		done
		if test -z "$LDAPINC"
		then
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([Unable to locate ldap.h, try specifying with --with-auth-ldap])
		fi
	fi
else
	AUTHALIB="modules/.libs/libauth_sql.a"
	AUTHLTLIB="modules/libauth_sql.la"
	AC_MSG_RESULT([using SQL authentication])
fi
])

dnl DBMAIL_CHECK_LDAP_LIBS
dnl
AC_DEFUN([DBMAIL_CHECK_LDAP_LIBS], [dnl
# Look for libs needed to link to LDAP first
if test ! "${authldapheadername-x}" = "x"
then
	AC_CHECK_LIB(ldap,ldap_bind,[ LDAPLIB="-lldap"], [LDAPLIB=""])
	if test -z "$LDAPLIB"
	then
		AC_MSG_ERROR([ Unable to link against ldap.  It appears you are missing the development libraries or they aren't in your linker's path ])
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
    ac_compile_warnings_opt='-W -Wall -Wpointer-arith -Wstrict-prototypes'
  fi
  CFLAGS="$CFLAGS $ac_compile_warnings_opt"
  ac_compile_warnings_msg="$ac_compile_warnings_msg $ac_compile_warnings_opt for C"
fi
AC_MSG_RESULT($ac_compile_warnings_msg)
unset ac_compile_warnings_msg
unset ac_compile_warnings_opt
])

dnl DBMAIL_CHECK_GLIB
dnl
AC_DEFUN([DBMAIL_CHECK_GLIB], [dnl
#Look for include files and libs needed to link
#use the configuration utilities (pkg-config for this)
AC_PATH_PROG(glibconfig,pkg-config)
if test [ -z "$glibconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GLib headers])
	ac_glib_cflags=`${glibconfig} --cflags glib-2.0`
	if test -z "$ac_glib_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib development files])
	fi
 
	CFLAGS="$CFLAGS $ac_glib_cflags"
	AC_MSG_RESULT([$ac_glib_cflags])
        AC_MSG_CHECKING([Glib libraries])
	ac_glib_libs=`${glibconfig} --libs glib-2.0`
	if test -z "$ac_glib_libs"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib libaries])
	fi
 

	LDFLAGS="$LDFLAGS $ac_glib_libs"
        AC_MSG_RESULT([$ac_glib_libs])
fi
])

dnl DBMAIL_CHECK_GMIME
dnl
AC_DEFUN([DBMAIL_CHECK_GMIME], [dnl
#Look for include files and libs needed to link
#use the configuration utilities (pkg-config for this)
AC_PATH_PROG(gmimeconfig,pkg-config)
if test [ -z "$gmimeconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GMime headers])
	ac_gmime_cflags=`${gmimeconfig} --cflags gmime-2.0`
	if test -z "$ac_gmime_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate gmime development files])
	else
		CFLAGS="$CFLAGS $ac_gmime_cflags"
		AC_MSG_RESULT([$ac_gmime_cflags])
	fi
	
        AC_MSG_CHECKING([GMime libraries])
	ac_gmime_libs=`${gmimeconfig} --libs gmime-2.0`
	if test -z "$ac_gmime_libs"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate gmime libaries])
	else
		LDFLAGS="$LDFLAGS $ac_gmime_libs"
        	AC_MSG_RESULT([$ac_gmime_libs])
	fi
fi
])

# ----------------------------------------------------------------
# DBMAIL_CHECK_GC
# I cheated I copied from w3m's acinclude.m4 :)
# Modified for DBMAIL by Dan Weber
# ----------------------------------------------------------------
AC_DEFUN([DBMAIL_CHECK_GC],
[AC_MSG_CHECKING(for --with-gc)
AC_ARG_WITH(gc,
 [  --with-gc[=PREFIX]        libgc PREFIX],
 [test x"$with_gc" = xno && with_gc="no"],
 [with_gc="no"])
 AC_MSG_RESULT($with_gc)
# Don't check for gc if not appended to command line
 if test x"$with_gc" = xyes
 then
 test x"$with_gc" = xyes && with_gc="/usr /usr/local ${HOME}"
 unset ac_cv_header_gc_h
  AC_CHECK_HEADER(gc/gc.h)
 if test x"$ac_cv_header_gc_h" = xno; then
   AC_MSG_CHECKING(GC header location)
   AC_MSG_RESULT($with_gc)
   gcincludedir=no
    for dir in $with_gc; do
     for inc in include include/gc; do
       cflags="$CFLAGS"
       CFLAGS="$CFLAGS -I$dir/$inc -DUSE_GC=1"
       AC_MSG_CHECKING($dir/$inc)
       unset ac_cv_header_gc_h
       AC_CHECK_HEADER(gc/gc.h, [gcincludedir="$dir/$inc"; CFLAGS="$CFLAGS -I$dir/$inc -DUSE_GC=1"; break])
       CFLAGS="$cflags"
     done
     if test x"$gcincludedir" != xno; then
       break;
     fi
   done
   if test x"$gcincludedir" = xno; then
     AC_MSG_ERROR([gc/gc.h not found])
   fi
 else
  cflags="$CFLAGS -DUSE_GC=1"
  CFLAGS="$cflags"
 fi
 unset ac_cv_lib_gc_GC_init
 AC_CHECK_LIB(gc, GC_init, [LIBS="$LIBS -lgc"])
 if test x"$ac_cv_lib_gc_GC_init" = xno; then
    AC_MSG_CHECKING(GC library location)
    AC_MSG_RESULT($with_gc)
    gclibdir=no
    for dir in $with_gc; do
      ldflags="$LDFLAGS"
      LDFLAGS="$LDFLAGS -L$dir/lib"
      AC_MSG_CHECKING($dir)
      unset ac_cv_lib_gc_GC_init
      AC_CHECK_LIB(gc, GC_init, [gclibdir="$dir/lib"; LIBS="$LIBS -L$dir/lib -lgc"; break])
      LDFLAGS="$ldflags"
    done
    if test x"$gclibdir" = xno; then
      AC_MSG_ERROR([libgc not found])
    fi
 fi
fi])



# ripped from check.m4

dnl DBMAIL_PATH_CHECK([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for check, and define CHECK_CFLAGS and CHECK_LIBS
dnl

AC_DEFUN([DBMAIL_PATH_CHECK],
[
  AC_ARG_WITH(check,
  [  --with-check=PATH       prefix where check is installed [default=auto]],
  [test x"$with_check" = xno && with_check="no"],
  [with_check="no"])
  
if test "x$with_check" != xno; then
  min_check_version=ifelse([$1], ,0.8.2,$1)

  AC_MSG_CHECKING(for check - version >= $min_check_version)

  if test x$with_check = xno; then
    AC_MSG_RESULT(disabled)
    ifelse([$3], , AC_MSG_ERROR([disabling check is not supported]), [$3])
  else
    if test "x$with_check" = xyes; then
      CHECK_CFLAGS=""
      CHECK_LIBS="-lcheck"
    else
      CHECK_CFLAGS="-I$with_check/include"
      CHECK_LIBS="-L$with_check/lib -lcheck"
    fi

    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"

    CFLAGS="$CFLAGS $CHECK_CFLAGS"
    LIBS="$CHECK_LIBS $LIBS"

    rm -f conf.check-test
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>

#include <check.h>

int main ()
{
  int major, minor, micro;
  char *tmp_version;

  system ("touch conf.check-test");

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = strdup("$min_check_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_check_version");
     return 1;
   }
    
  if ((CHECK_MAJOR_VERSION != check_major_version) ||
      (CHECK_MINOR_VERSION != check_minor_version) ||
      (CHECK_MICRO_VERSION != check_micro_version))
    {
      printf("\n*** The check header file (version %d.%d.%d) does not match\n",
	     CHECK_MAJOR_VERSION, CHECK_MINOR_VERSION, CHECK_MICRO_VERSION);
      printf("*** the check library (version %d.%d.%d).\n",
	     check_major_version, check_minor_version, check_micro_version);
      return 1;
    }

  if ((check_major_version > major) ||
      ((check_major_version == major) && (check_minor_version > minor)) ||
      ((check_major_version == major) && (check_minor_version == minor) && (check_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** An old version of check (%d.%d.%d) was found.\n",
             check_major_version, check_minor_version, check_micro_version);
      printf("*** You need a version of check being at least %d.%d.%d.\n", major, minor, micro);
      printf("***\n"); 
      printf("*** If you have already installed a sufficiently new version, this error\n");
      printf("*** probably means that the wrong copy of the check library and header\n");
      printf("*** file is being found. Rerun configure with the --with-check=PATH option\n");
      printf("*** to specify the prefix where the correct version was installed.\n");
    }

  return 1;
}
],, no_check=yes, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"

    if test "x$no_check" = x ; then
      AC_MSG_RESULT(yes)
      ifelse([$2], , :, [$2])
    else
      AC_MSG_RESULT(no)
      if test -f conf.check-test ; then
        :
      else
        echo "*** Could not run check test program, checking why..."
        CFLAGS="$CFLAGS $CHECK_CFLAGS"
        LIBS="$CHECK_LIBS $LIBS"
        AC_TRY_LINK([
#include <stdio.h>
#include <stdlib.h>

#include <check.h>
], ,  [ echo "*** The test program compiled, but did not run. This usually means"
        echo "*** that the run-time linker is not finding check. You'll need to set your"
        echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
        echo "*** to the installed location  Also, make sure you have run ldconfig if that"
        echo "*** is required on your system"
	echo "***"
        echo "*** If you have an old version installed, it is best to remove it, although"
        echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
      [ echo "*** The test program failed to compile or link. See the file config.log for"
        echo "*** the exact error that occured." ])
      
        CFLAGS="$ac_save_CFLAGS"
        LIBS="$ac_save_LIBS"
      fi

      CHECK_CFLAGS=""
      CHECK_LIBS=""

      rm -f conf.check-test
      ifelse([$3], , AC_MSG_ERROR([check not found]), [$3])
    fi
fi

    AC_SUBST(CHECK_CFLAGS)
    AC_SUBST(CHECK_LIBS)

    rm -f conf.check-test

  fi
])
