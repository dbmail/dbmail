
AC_DEFUN([DM_MSG_CONFIGURE_START], [dnl
AC_MSG_RESULT([
This is dbmail's GNU configure script.
])
])

AC_DEFUN([DM_MSG_CONFIGURE_RESULTS], [dnl
AC_MSG_RESULT([
 PREFIX                     $prefix
 USE_DM_GETOPT:             $USE_DM_GETOPT
 CFLAGS:                    $CFLAGS
 GLIB:                      $ac_glib_libs
 GMIME:                     $ac_gmime_libs
 SIEVE:                     $SIEVEINC$SIEVELIB
 LDAP:                      $LDAPINC$LDAPLIB
 SHARED:                    $enable_shared
 STATIC:                    $enable_static
 CHECK:                     $CHECK_LIBS
 SOCKETS:                   $SOCKETLIB
 MATH:                      $MATHLIB
 MHASH:                     $MHASHLIB
 LIBEVENT:                  $EVENTLIB
 OPENSSL:                   $SSLLIB
 SYSTEMD:                   $SYSTEMD_LIBS
 ZDB:                       $ZDBLIB
 JEMALLOC:                  $JEMALLOCLIB

])
])


AC_DEFUN([DM_DEFINES],[dnl
AC_ARG_WITH(logdir,
	[  --with-logdir           use logdir for logfiles],
	logdirname="$withval")

if test [ "x$logdirname" = "x" ]; then
	AC_DEFINE_UNQUOTED([DEFAULT_LOG_DIR], LOCALSTATEDIR"/log" , [Log directory])
else
	AC_DEFINE_UNQUOTED([DEFAULT_LOG_DIR], "$logdirname", [Log directory])
fi
AC_DEFINE_UNQUOTED([DM_PWD], "$ac_pwd", [Build directory])
AC_DEFINE_UNQUOTED([DM_VERSION], "$PACKAGE_VERSION", [DBMail Version])
])
	
AC_DEFUN([DM_SET_SHARED_OR_STATIC], [dnl
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

AC_DEFUN([DM_SIEVE_INC],[
    AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]])],
    [$1],
    [$2])
])
AC_DEFUN([DM_SIEVE_LIB],[
    AC_LINK_IFELSE([
    AC_LANG_PROGRAM([[
        #define NULL 0
        #include <sieve2.h>]])],
    [$1],
    [$2])
])

AC_DEFUN([DM_SIEVE_CONF], [dnl
WARN=0

AC_ARG_WITH(sieve,[  --with-sieve=PATH	  path to libSieve base directory (e.g. /usr/local or /usr)],
	[lookforsieve="$withval"],[lookforsieve="no"])

SORTALIB="modules/.libs/libsort_null.a"
SORTLTLIB="modules/libsort_null.la"

if test [ "x$lookforsieve" != "xno" ]; then
    if test [ "x$lookforsieve" != "xyes" ]; then
        sieveprefixes=$lookforsieve
    fi
    AC_MSG_CHECKING([for libSieve headers])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

	if test [ "x$lookforsieve" = "xyes" ]; then
            DM_SIEVE_INC([SIEVEINC=""], [SIEVEINC="failed"])
            if test [ "x$SIEVEINC" != "xfailed" ]; then
                break
            fi
        fi
 
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            DM_SIEVE_INC([SIEVEINC="-I$TEST_PATH"], [SIEVEINC="failed"])
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

    AC_MSG_CHECKING([for libSieve libraries])
    STOP_LOOKING_FOR_SIEVE=""
    while test [ -z $STOP_LOOKING_FOR_SIEVE ]; do 

        if test [ "x$lookforsieve" = "xyes" ]; then
            DM_SIEVE_LIB([SIEVELIB="-lsieve"], [SIEVELIB="failed"])
            if test [ "x$SIEVELIB" != "xfailed" ]; then
                break
            fi
        fi
 
        for TEST_PATH in $sieveprefixes; do
	    TEST_PATH="$TEST_PATH/lib"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -L$TEST_PATH $SIEVEINC"
            DM_SIEVE_LIB([SIEVELIB="-L$TEST_PATH -lsieve"], [SIEVELIB="failed"])
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

AC_DEFUN([DM_LDAP_CONF], [dnl
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

    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
	if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            AC_CHECK_HEADERS([ldap.h],[LDAPINC=""], [LDAPINC="failed"])
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break
            fi
        fi
 
        dnl Explicitly test paths from --with-ldap or configure.in
        for TEST_PATH in $ldapprefixes; do
	    TEST_PATH="$TEST_PATH/include"
            SAVE_CFLAGS=$CFLAGS
            CFLAGS="$CFLAGS -I$TEST_PATH"
            AC_CHECK_HEADERS([ldap.h],[LDAPINC="-I$TEST_PATH"], [LDAPINC="failed"])
            CFLAGS=$SAVE_CFLAGS
            if test [ "x$LDAPINC" != "xfailed" ]; then
                break 2
            fi
        done

        STOP_LOOKING_FOR_LDAP="done"
    done

    if test [ "x$LDAPINC" = "xfailed" ]; then
        AC_MSG_ERROR([Could not find LDAP headers.])
    fi

    STOP_LOOKING_FOR_LDAP=""
    while test [ -z $STOP_LOOKING_FOR_LDAP ]; do 

        dnl See if we already have the paths we need in the environment.
	dnl ...but only if --with-ldap was given without a specific path.
        if ( test [ "x$lookforldap" = "xyes" ] || test [ "x$lookforauthldap" = "xyes" ] ); then
            AC_CHECK_HEADERS([ldap.h],[LDAPLIB="-lldap"], [LDAPLIB="failed"])
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
            AC_CHECK_HEADERS([ldap.h],[LDAPLIB="-L$TEST_PATH -lldap"], [LDAPLIB="failed"])
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
        AC_DEFINE([AUTHLDAP], 1, [Define if LDAP will be used.])
        AC_SEARCH_LIBS(ldap_initialize, ldap, AC_DEFINE([HAVE_LDAP_INITIALIZE], 1, [ldap_initialize() can be used instead of ldap_init()]))
        AC_SUBST(LDAPLIB)
        AC_SUBST(LDAPINC)
        AUTHALIB="modules/.libs/libauth_ldap.a"
        AUTHLTLIB="modules/libauth_ldap.la"
    fi
fi
])

AC_DEFUN([DM_CHECK_JEMALLOC], [dnl
	AC_ARG_WITH(jemalloc,[  --with-jemalloc=PATH	  path to libjemalloc base directory (e.g. /usr/local or /usr)],
		[lookforjemalloc="$withval"],[lookforjemalloc="yes"])
	if test [ "x$lookforjemalloc" != "xno" ] ; then
		if test [ "x$lookforjemalloc" = "xyes" ] ; then 
			CFLAGS="$CFLAGS -I${ac_default_prefix}/include/jemalloc -I/usr/include/jemalloc"
		else
			CFLAGS="$CFLAGS -I${lookforjemalloc}/include/jemalloc"
		fi
	fi
	AC_CHECK_HEADERS([jemalloc.h jemalloc_defs.h],
		[JEMALLOCLIB="-ljemalloc"], 
		[JEMALLOCLIB="no"],
	[[
#include <jemalloc.h>
#include <jemalloc_defs.h>
	]])
	if test [ "x$JEMALLOCLIB" != "xno" ]; then
		LDFLAGS="$LDFLAGS $JEMALLOCLIB"
		AC_DEFINE([USEJEMALLOC], 1, [Define if jemalloc will be used.])
	fi
])

AC_DEFUN([DM_CHECK_ZDB], [dnl
	AC_ARG_WITH(zdb,[  --with-zdb=PATH	  path to libzdb base directory (e.g. /usr/local or /usr)],
		[lookforzdb="$withval"],[lookforzdb="no"])
	if test [ "x$lookforzdb" = "xno" ] ; then
		CFLAGS="$CFLAGS -I${ac_default_prefix}/include/zdb -I/usr/include/zdb"
	else
		CFLAGS="$CFLAGS -I${lookforzdb}/include/zdb"
	fi
	AC_CHECK_HEADERS([URL.h ResultSet.h PreparedStatement.h Connection.h ConnectionPool.h SQLException.h],
		[ZDBLIB="-lzdb"], 
		[ZDBLIB="failed"],
	[[
#include <URL.h>
#include <ResultSet.h>
#include <PreparedStatement.h>
#include <Connection.h>
#include <ConnectionPool.h>
#include <SQLException.h>	
	]])
	if test [ "x$ZDBLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find ZDB library.])
	else
		LDFLAGS="$LDFLAGS $ZDBLIB"
	fi
])

AC_DEFUN([DM_SET_DEFAULT_CONFIGURATION], [dnl
	DM_DEFAULT_CONFIGURATION=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  dbmail.conf`
])


AC_DEFUN([DM_CHECK_MATH], [dnl
	AC_CHECK_HEADERS([math.h],[MATHLIB="-lm"], [MATHLIB="failed"])
	if test [ "x$MATHLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find MATH library.])
	else
		LDFLAGS="$LDFLAGS $MATHLIB"
	fi
])

AC_DEFUN([DM_CHECK_MHASH], [dnl
	AC_CHECK_HEADERS([mhash.h],[MHASHLIB="-lmhash"], [MHASHLIB="failed"])
	if test [ "x$MHASHLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find MHASH library.])
	else
		LDFLAGS="$LDFLAGS $MHASHLIB"
	fi
])

AC_DEFUN([DM_CHECK_EVENT], [
	AC_CHECK_HEADERS([event.h], [EVENTLIB="-levent_pthreads -levent"],[EVENTLIB="failed"], [#include <event2/event.h>])
	if test [ "x$EVENTLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find EVENT library.])
	else
		LDFLAGS="$LDFLAGS $EVENTLIB"
	fi
])

AC_DEFUN([DM_CHECK_SSL], [
	AC_CHECK_HEADERS([openssl/ssl.h],
	 [SSLLIB=`pkg-config --libs openssl 2>/dev/null`],[SSLLIB="failed"])
	if test [ "x$SSLLIB" = "xfailed" ]; then
		AC_MSG_ERROR([Could not find OPENSSL library.])
	else
		LDFLAGS="$LDFLAGS $SSLLIB"
	fi
])

AC_DEFUN([DM_CHECK_SYSTEMD], [
	PKG_CHECK_MODULES([SYSTEMD], [libsystemd-daemon], , [
		PKG_CHECK_MODULES([SYSTEMD], [systemd >= 230])
	])
	if test [ -n $SYSTEMD_LIBS ]; then
		AC_DEFINE([HAVE_SYSTEMD], [1], [Define if systemd will be used])
		LDFLAGS="$LDFLAGS $SYSTEMD_LIBS"
	fi
])

AC_DEFUN([AC_COMPILE_WARNINGS],
[AC_MSG_CHECKING(maximum warning verbosity option)
if test -n "$CXX"; then
	if test "$GXX" = "yes"; then
		ac_compile_warnings_opt='-Wall'
	fi
	CXXFLAGS="$CXXFLAGS $ac_compile_warnings_opt"
	ac_compile_warnings_msg="$ac_compile_warnings_opt for C++"
fi
if test -n "$CC"; then
	if test "$GCC" = "yes"; then
		ac_compile_warnings_opt='-W -Wall -Wpointer-arith -Wstrict-prototypes'
	fi
	CFLAGS="$CFLAGS $ac_compile_warnings_opt"
	ac_compile_warnings_msg="$ac_compile_warnings_msg $ac_compile_warnings_opt for C"
fi

AC_MSG_RESULT($ac_compile_warnings_msg)
unset ac_compile_warnings_msg
unset ac_compile_warnings_opt
])

AC_DEFUN([DM_CHECK_GLIB], [dnl
AC_PATH_PROG(glibconfig,pkg-config)
if test [ -z "$glibconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GLib headers])
	ac_glib_cflags=`${glibconfig} --cflags glib-2.0 --cflags gmodule-2.0 --cflags gthread-2.0 2>/dev/null`
	if test -z "$ac_glib_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib development files])
	fi
 
	CFLAGS="$CFLAGS $ac_glib_cflags"
	AC_MSG_RESULT([$ac_glib_cflags])
        AC_MSG_CHECKING([Glib libraries])
	ac_glib_libs=`${glibconfig} --libs glib-2.0 --libs gmodule-2.0 --libs gthread-2.0 2>/dev/null`
	if test -z "$ac_glib_libs"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate glib libaries])
	fi
 	ac_glib_minvers="2.16"
	AC_MSG_CHECKING([GLib version >= $ac_glib_minvers])
	ac_glib_vers=`${glibconfig}  --atleast-version=$ac_glib_minvers glib-2.0 2>/dev/null && echo yes`
	if test -z "$ac_glib_vers"
	then
		AC_MSG_ERROR([At least GLib version $ac_glib_minvers is required.])
	fi

	LDFLAGS="$LDFLAGS $ac_glib_libs"
        AC_MSG_RESULT([$ac_glib_libs])
fi
])

AC_DEFUN([DM_CHECK_GMIME], [dnl
AC_PATH_PROG(gmimeconfig,pkg-config)
if test [ -z "$gmimeconfig" ]
then
	AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
else
	AC_MSG_CHECKING([GMime headers])
	ac_gmime_cflags=`${gmimeconfig} --cflags gmime-2.6 2>/dev/null|| ${gmimeconfig} --cflags gmime-2.4 2>/dev/null`
	if test -z "$ac_gmime_cflags"
	then
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([Unable to locate gmime development files])
	else
		CFLAGS="$CFLAGS $ac_gmime_cflags"
		AC_MSG_RESULT([$ac_gmime_cflags])
	fi
	
        AC_MSG_CHECKING([GMime libraries])
	ac_gmime_libs=`${gmimeconfig} --libs gmime-2.6 2>/dev/null|| ${gmimeconfig} --libs gmime-2.4 2>/dev/null`
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

AC_DEFUN([DM_PATH_CHECK],[dnl
  AC_ARG_WITH(check,
  [  --with-check=PATH       prefix where check is installed [default=auto]],
  [test x"$with_check" = xno && with_check="no"],
  [with_check="no"])
  
if test "x$with_check" != xno; then
	AC_PATH_PROG(checkconfig,pkg-config)
	if test [ -z "$checkconfig" ]
	then
		AC_MSG_ERROR([pkg-config executable not found. Make sure pkg-config is in your path])
	else
		AC_MSG_CHECKING([Check])
		CHECK_LIBS=`${checkconfig} --libs check 2>/dev/null`
		if test -z "$CHECK_LIBS"
		then
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([Unable to locate check libaries])
		fi
		LDFLAGS="$LDFLAGS $CHECK_LIBS"
		AC_MSG_RESULT([$CHECK_LIBS])
	fi
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

AC_DEFUN([DM_SET_SQLITECREATE], [dnl
	SQLITECREATE=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/create_tables.sqlite`
])

# register upgrades
AC_DEFUN([DM_UPGRADE_STEPS], [dnl
	PGSQL_32001=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/postgresql/upgrades/32001.psql`
	MYSQL_32001=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/mysql/upgrades/32001.mysql`
	SQLITE_32001=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/upgrades/32001.sqlite`
	AC_SUBST(PGSQL_32001)
	AC_SUBST(MYSQL_32001)
	AC_SUBST(SQLITE_32001)

	PGSQL_32002=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/postgresql/upgrades/32002.psql`
	MYSQL_32002=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/mysql/upgrades/32002.mysql`
	SQLITE_32002=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/upgrades/32002.sqlite`
	AC_SUBST(PGSQL_32002)
	AC_SUBST(MYSQL_32002)
	AC_SUBST(SQLITE_32002)

	PGSQL_32003=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/postgresql/upgrades/32003.psql`
	MYSQL_32003=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/mysql/upgrades/32003.mysql`
	SQLITE_32003=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/upgrades/32003.sqlite`
	AC_SUBST(PGSQL_32003)
	AC_SUBST(MYSQL_32003)
	AC_SUBST(SQLITE_32003)

	PGSQL_32004=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/postgresql/upgrades/32004.psql`
	MYSQL_32004=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/mysql/upgrades/32004.mysql`
	SQLITE_32004=`sed -e 's/\"/\\\"/g' -e 's/^/\"/' -e 's/$/\\\n\"/' -e '$!s/$/ \\\\/'  sql/sqlite/upgrades/32004.sqlite`
	AC_SUBST(PGSQL_32004)
	AC_SUBST(MYSQL_32004)
	AC_SUBST(SQLITE_32004)
])
