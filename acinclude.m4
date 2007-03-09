
dnl  DBMAIL_MSG_CONFIGURE_START()
dnl
AC_DEFUN([DBMAIL_MSG_CONFIGURE_START], [dnl
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
DM_LOGDIR:     $DM_LOGDIR
DM_CONFDIR:    $DM_CONFDIR
DM_STATEDIR:   $DM_STATEDIR
USE_DM_GETOPT: $USE_DM_GETOPT
CFLAGS:        $CFLAGS
GLIB:          $ac_glib_libs
GMIME:         $ac_gmime_libs
MYSQL:         $MYSQLLIB
PGSQL:         $PGSQLLIB
SQLITE:        $SQLITELIB
SIEVE:         $SIEVEINC$SIEVELIB
LDAP:          $LDAPINC$LDAPLIB
SHARED:        $enable_shared
STATIC:        $enable_static
CHECK:         $with_check
SOCKETS:       $SOCKETLIB
])
])


AC_DEFUN([DM_DEFINES],[dnl
AC_ARG_WITH(logdir,
	[  --with-logdir           use logdir for logfiles],
	logdirname="$withval")
  if test "x$logdirname" = "x"
  then
  	DM_LOGDIR="/var/log"
  else
  	DM_LOGDIR="$logdirname"
  fi
  if test "x$localstatedir" = 'x${prefix}/var'
  then
  	DM_STATEDIR='/var/run'
  else
  	DM_STATEDIR=$localstatedir
  fi
  if test "x$sysconfdir" = 'x${prefix}/etc'
  then
  	DM_CONFDIR='/etc'
  else
  	DM_CONFDIR=$sysconfdir
  fi
  if test "x$prefix" = "xNONE"
  then
  	AC_DEFINE_UNQUOTED([PREFIX], "$ac_default_prefix", [Prefix to the installed path])
  else
  	AC_DEFINE_UNQUOTED([PREFIX], "$prefix", [Prefix to the installed path])
  fi
])
	
dnl DBMAIL_CHECK_SHARED_OR_STATIC
AC_DEFUN([DBMAIL_SET_SHARED_OR_STATIC], [dnl
if test [ "$enable_shared" = "yes" -a "$enable_static" = "yes" ]; then
     AC_MSG_ERROR([
     You cannot enable both shared and static build.
     Please choose only one to enable.
])
fi
if test [ "$enable_shared" = "no" -a "$enable_static" = "no" ]; then
  enable_shared="yes"
fi

])


dnl DBMAIL_BOTH_SQL_CHECK
AC_DEFUN([DBMAIL_BOTH_SQL_CHECK], [dnl

usemysql="no"
usepgsql="no"
usesqlite="no"

AC_ARG_WITH(mysql,
            [  --with-mysql            use MySQL as database. Uses mysql_config
	       		  for finding includes and libraries],
            usemysql="$withval")
AC_ARG_WITH(pgsql,
	    [  --with-pgsql            use PostgreSQL as database. 
                          Uses pg_config for finding includes and libraries],
            usepgsql="$withval")
AC_ARG_WITH(sqlite,
	    [  --with-sqlite           use SQLite3 as database. 
                          Uses pkg-config for finding includes and libraries],
            usesqlite="$withval")

if test [ ! "$usemysql" = "yes" -a ! "$usepgsql" = "yes" -a ! "$usesqlite" = "yes" ]; then
     AC_MSG_ERROR([You have to specify --with-mysql, --with-pgsql or --with-sqlite to build.])
fi

])

dnl DBMAIL_CHECK_SQL_LIBS
AC_DEFUN([DBMAIL_CHECK_SQL_LIBS], [dnl
if test [ "$usemysql" = "yes" ]; then
    AC_PATH_PROG(mysqlconfig,mysql_config)
    if test [ -z "$mysqlconfig" ]; then
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
if test [ "$usepgsql" = "yes" ]; then
    AC_PATH_PROG(pgsqlconfig,pg_config)
    if test [ -z "$pgsqlconfig" ]; then
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
if test [ "$usesqlite" = "yes" ]; then
    AC_PATH_PROG(sqliteconfig,pkg-config)
    if test [ -z "$sqliteconfig" ]; then
        AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
    else
	AC_MSG_CHECKING([SQLite3 headers])
	SQLITEINC=`${sqliteconfig} --cflags sqlite3 --errors-to-stdout`
	if test [ $? != 0 ]; then
        	AC_MSG_ERROR([$SQLITEINC])
	fi
	AC_MSG_RESULT([$SQLITEINC])
        AC_MSG_CHECKING([SQLite libraries])
        SQLITELIB=`${sqliteconfig} --libs sqlite3 --errors-to-stdout`
	if test [ $? != 0 ]; then
        	AC_MSG_ERROR([$SQLITEINC])
	fi
        SQLITEALIB="modules/.libs/libsqlite.a"
	SQLITELTLIB="modules/libsqlite.la"
        AC_MSG_RESULT([$SQLITELIB])
    	SQLITECREATE=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\" \\\\/'  sql/sqlite/create_tables.sqlite`
    fi
fi
])


dnl Check for Sieve header.
AC_DEFUN([DBMAIL_CHECK_SIEVE_INC],[
    AC_COMPILE_IFELSE(
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]]),
    [$1],
    [$2])
])
dnl Check for Sieve library.
AC_DEFUN([DBMAIL_CHECK_SIEVE_LIB],[
    AC_LINK_IFELSE(
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]]),
    [$1],
    [$2])
])

dnl DBMAIL_SIEVE_CONF
dnl check for sieve sorting
AC_DEFUN([DBMAIL_SIEVE_CONF], [dnl
WARN=0

AC_ARG_WITH(sieve,[  --with-sieve=PATH	  path to libSieve base directory (e.g. /usr/local or /usr)],
	[lookforsieve="$withval"],[lookforsieve="no"])


dnl Set the default sort modules to null, as
dnl the user may not have asked for Sieve at all.
SORTALIB="modules/.libs/libsort_null.a"
SORTLTLIB="modules/libsort_null.la"

dnl Go looking for the Sieve headers and libraries.
if test [ "x$lookforsieve" != "xno" ]; then

    dnl We were given a specific path. Only look there.
    if test [ "x$lookforsieve" != "xyes" ]; then
        sieveprefixes=$lookforsieve
    fi

    dnl Look for Sieve headers.
    AC_MSG_CHECKING([for libSieve headers])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-sieve was given without a specific path.
	if test [ "x$lookforsieve" = "xyes" ]; then
            DBMAIL_CHECK_SIEVE_INC([SIEVEINC=""], [SIEVEINC="failed"])
            if test [ "x$SIEVEINC" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-sieve or configure.in
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            DBMAIL_CHECK_SIEVE_INC([SIEVEINC="-I$TEST_PATH"], [SIEVEINC="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$SIEVEINC" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_SIEVE="done"
    done

    if test [ "x$SIEVEINC" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find libSieve headers.])
    else
        AC_MSG_RESULT($SIEVEINC)
    fi

    dnl Look for Sieve libraries.
    AC_MSG_CHECKING([for libSieve libraries])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-sieve was given without a specific path.
        if test [ "x$lookforsieve" = "xyes" ]; then
            DBMAIL_CHECK_SIEVE_LIB([SIEVELIB="-lsieve"], [SIEVELIB="failed"])
            if test [ "x$SIEVELIB" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-sieve or configure.in
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/lib"
            SAVE_CFLAGS=$CFLAGS
	    dnl The headers might be in a funny place, so we need to use -Ipath
            CFLAGS="$CFLAGS -L$TEST_PATH $SIEVEINC"
            DBMAIL_CHECK_SIEVE_LIB([SIEVELIB="-L$TEST_PATH -lsieve"], [SIEVELIB="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$SIEVELIB" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_SIEVE="done"
    done

    if test [ "x$SIEVELIB" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find libSieve library.])
    else
        dnl Found it, set the settings.
        AC_MSG_RESULT($SIEVELIB)
        AC_DEFINE([SIEVE], 1, [Define if Sieve sorting will be used.])
        AC_SUBST(SIEVELIB)
        AC_SUBST(SIEVEINC)
        SORTALIB="modules/.libs/libsort_sieve.a"
        SORTLTLIB="modules/libsort_sieve.la"
    fi
fi
])


dnl Check for LDAP header.
AC_DEFUN([DBMAIL_CHECK_LDAP_INC],[
    AC_COMPILE_IFELSE(
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <ldap.h>]]),
    [$1],
    [$2])
])
dnl Check for LDAP library.
AC_DEFUN([DBMAIL_CHECK_LDAP_LIB],[
    AC_LINK_IFELSE(
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <ldap.h>]]),
    [$1],
    [$2])
])

dnl DBMAIL_LDAP_CONF
dnl check for ldap or sql authentication
AC_DEFUN([DBMAIL_LDAP_CONF], [dnl
WARN=0

dnl --with-auth-ldap is deprecated as of DBMail 2.2.2
AC_ARG_WITH(auth-ldap,[  --with-auth-ldap=PATH	  deprecated, use --with-ldap],
	[lookforauthldap="$withval"],[lookforauthldap="no"])

AC_ARG_WITH(ldap,[  --with-ldap=PATH	  path to LDAP base directory (e.g. /usr/local or /usr)],
	[lookforldap="$withval"],[lookforldap="no"])

dnl Set the default auth modules to sql, as
dnl the user may not have asked for LDAP at all.
AUTHALIB="modules/.libs/libauth_sql.a"
AUTHLTLIB="modules/libauth_sql.la"

dnl Go looking for the LDAP headers and libraries.
if ( test [ "x$lookforldap" != "xno" ] || test [ "x$lookforauthldap" != "xno" ] ); then

    dnl Support the deprecated --with-auth-ldap per comment above.
    if ( test [ "x$lookforauthldap" != "xyes" ] && test [ "x$lookforauthldap" != "xno" ] ); then
        lookforldap=$lookforauthldap
    fi

    dnl We were given a specific path. Only look there.
    if ( test [ "x$lookforldap" != "xyes" ] && test [ "x$lookforldap" != "xno" ] ); then
        ldapprefixes=$lookforldap
    fi

    dnl Look for LDAP headers.
    AC_MSG_CHECKING([for LDAP headers])
    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
	if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            DBMAIL_CHECK_LDAP_INC([LDAPINC=""], [LDAPINC="failed"])
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-ldap or configure.in
        for TEST_PATH in $ldapprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            DBMAIL_CHECK_LDAP_INC([LDAPINC="-I$TEST_PATH"], [LDAPINC="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_LDAP="done"
    done

    if test [ "x$LDAPINC" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find LDAP headers.])
    else
        AC_MSG_RESULT($LDAPINC)
    fi

    dnl Look for LDAP libraries.
    AC_MSG_CHECKING([for LDAP libraries])
    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
        if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            DBMAIL_CHECK_LDAP_LIB([LDAPLIB="-lldap"], [LDAPLIB="failed"])
            if test [ "x$LDAPLIB" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-ldap or configure.in
        for TEST_PATH in $ldapprefixes; do
	    TEST_PATH="$TEST_PATH/lib"
            SAVE_CFLAGS=$CFLAGS
	    dnl The headers might be in a funny place, so we need to use -Ipath
            CFLAGS="$CFLAGS -L$TEST_PATH $LDAPINC"
            DBMAIL_CHECK_LDAP_LIB([LDAPLIB="-L$TEST_PATH -lldap"], [LDAPLIB="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$LDAPLIB" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_LDAP="done"
    done

    if test [ "x$LDAPLIB" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find LDAP library.])
    else
        AC_MSG_RESULT($LDAPLIB)
        AC_DEFINE([AUTHLDAP], 1, [Define if LDAP will be used.])
        AC_SEARCH_LIBS(ldap_initialize, ldap, AC_DEFINE([HAVE_LDAP_INITIALIZE], 1, [ldap_initialize() can be used instead of ldap_init()]))
        AC_SUBST(LDAPLIB)
        AC_SUBST(LDAPINC)
        AUTHALIB="modules/.libs/libauth_ldap.a"
        AUTHLTLIB="modules/libauth_ldap.la"
    fi
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

# getopt.m4 serial 12
dnl Copyright (C) 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

# The getopt module assume you want GNU getopt, with getopt_long etc,
# rather than vanilla POSIX getopt.  This means your your code should
# always include <getopt.h> for the getopt prototypes.

AC_DEFUN([gl_GETOPT_SUBSTITUTE],
[
  dnl Modified for DBMail, which does not use the Gnulib getopt.
  dnl CFLAGS="$CFLAGS -DUSE_DM_GETOPT"
  AC_DEFINE([USE_DM_GETOPT], 1, [Define if our local getopt will be used.])
  USE_DM_GETOPT=1
])

AC_DEFUN([gl_GETOPT_CHECK_HEADERS],
[
  if test -z "$GETOPT_H"; then
    AC_CHECK_HEADERS([getopt.h], [], [GETOPT_H=getopt.h])
  fi

  if test -z "$GETOPT_H"; then
    AC_CHECK_FUNCS([getopt_long_only], [], [GETOPT_H=getopt.h])
  fi

  dnl BSD getopt_long uses an incompatible method to reset option processing,
  dnl and (as of 2004-10-15) mishandles optional option-arguments.
  if test -z "$GETOPT_H"; then
    AC_CHECK_DECL([optreset], [GETOPT_H=getopt.h], [], [#include <getopt.h>])
  fi

  dnl Solaris 10 getopt doesn't handle `+' as a leading character in an
  dnl option string (as of 2005-05-05).
  if test -z "$GETOPT_H"; then
    AC_CACHE_CHECK([for working GNU getopt function], [gl_cv_func_gnu_getopt],
      [AC_RUN_IFELSE(
	[AC_LANG_PROGRAM([#include <getopt.h>],
	   [[
	     char *myargv[3];
	     myargv[0] = "conftest";
	     myargv[1] = "-+";
	     myargv[2] = 0;
	     return getopt (2, myargv, "+a") != '?';
	   ]])],
	[gl_cv_func_gnu_getopt=yes],
	[gl_cv_func_gnu_getopt=no],
	[dnl cross compiling - pessimistically guess based on decls
	 dnl Solaris 10 getopt doesn't handle `+' as a leading character in an
	 dnl option string (as of 2005-05-05).
	 AC_CHECK_DECL([getopt_clip],
	   [gl_cv_func_gnu_getopt=no], [gl_cv_func_gnu_getopt=yes],
	   [#include <getopt.h>])])])
    if test "$gl_cv_func_gnu_getopt" = "no"; then
      GETOPT_H=getopt.h
    fi
  fi
])

AC_DEFUN([gl_GETOPT_IFELSE],
[
  AC_REQUIRE([gl_GETOPT_CHECK_HEADERS])
  AS_IF([test -n "$GETOPT_H"], [$1], [$2])
])

AC_DEFUN([gl_GETOPT], [gl_GETOPT_IFELSE([gl_GETOPT_SUBSTITUTE])])

# Prerequisites of lib/getopt*.
AC_DEFUN([gl_PREREQ_GETOPT],
[
  AC_CHECK_DECLS_ONCE([getenv])
])

dnl bsd_sockets.m4--which socket libraries do we need? 
dnl Derrick Brashear
dnl from Zephyr
dnl $Id: acinclude.m4 2456 2007-03-09 14:32:52Z paul $

dnl Hacked on by Rob Earhart to not just toss stuff in LIBS
dnl It now puts everything required for sockets into SOCKETLIB

AC_DEFUN([CMU_SOCKETS], [
	save_LIBS="$LIBS"
	SOCKETLIB=""
	AC_CHECK_FUNC(connect, :,
		AC_CHECK_LIB(nsl, gethostbyname,
			     SOCKETLIB="-lnsl $SOCKETLIB")
		AC_CHECK_LIB(socket, connect,
			     SOCKETLIB="-lsocket $SOCKETLIB")
	)
	LIBS="$SOCKETLIB $save_LIBS"
	AC_CHECK_FUNC(res_search, :,
                AC_CHECK_LIB(resolv, res_search,
                              SOCKETLIB="-lresolv $SOCKETLIB") 
        )
	LIBS="$SOCKETLIB $save_LIBS"
	AC_CHECK_FUNCS(dn_expand dns_lookup)
	LIBS="$save_LIBS"
	AC_SUBST(SOCKETLIB)
])

