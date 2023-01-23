# Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
# Copyright (C) 2004-2013 NFG Net Facilities Group support@nfg.nl
# Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
# Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

AC_INIT([dbmail], [3.3.1], [dbmail@dbmail.org])
AC_CONFIG_AUX_DIR(config)
AM_CONFIG_HEADER(config.h:config.in)
AC_CONFIG_MACRO_DIR([m4])

# ---------------------------------------------
DM_MSG_CONFIGURE_START
AM_INIT_AUTOMAKE([subdir-objects])
AM_MAINTAINER_MODE
AC_DEFINE(_GNU_SOURCE,'TRUE','Defined GNU SOURCE')

# ---------------------------------------------
# shared or static
# ---------------------------------------------
AM_ENABLE_SHARED(no)
AM_ENABLE_STATIC(no)
DM_SET_SHARED_OR_STATIC
AM_CONDITIONAL(SHARED, [ test "$enable_shared" = "yes" ])
AM_CONDITIONAL(STATIC, [ test "$enable_static" = "yes" ])

# ---------------------------------------------
DM_DEFINES
DM_LDAP_CONF
DM_SIEVE_CONF
DM_CHECK_GLIB
DM_CHECK_GMIME
DM_CHECK_MATH
DM_CHECK_MHASH
DM_CHECK_EVENT
DM_CHECK_SSL
DM_CHECK_ZDB
DM_CHECK_JEMALLOC
DM_PATH_CHECK
gl_GETOPT
CMU_SOCKETS

AM_CONDITIONAL(USE_DM_GETOPT, [test -n "$USE_DM_GETOPT"])
AM_CONDITIONAL(WITHCHECK, [test -n "$CHECK_LIBS"])

AC_ARG_ENABLE([manpages],
    AS_HELP_STRING([--enable-manpages], [Enable building and installation of man pages]))
AM_CONDITIONAL(MANPAGES, [ test "$enable_manpages" = "yes" ])

AC_ARG_ENABLE([systemd],
    AS_HELP_STRING([--enable-systemd], [Enable systemd support]))
AM_CONDITIONAL(SYSTEMD, [ test "$enable_systemd" = "yes" ])
if [ test "$enable_systemd" = "yes" ]; then
    DM_CHECK_SYSTEMD
fi

AC_PROG_CC
AC_COMPILE_WARNINGS
AC_C_CONST
AC_C_INLINE
AM_CONDITIONAL(SIEVE, [test -n "$SIEVELIB"])
AM_CONDITIONAL(LDAP, [test -n "$LDAPLIB"])
AC_C_BIGENDIAN
AC_CHECK_HEADERS(endian.h)
AC_CHECK_HEADERS(crypt.h)
AC_CHECK_LIB(crypt, crypt, [ CRYPTLIB="-lcrypt" ])

DM_SET_SQLITECREATE
AC_SUBST(SQLITECREATE)

DM_UPGRADE_STEPS

DM_SET_DEFAULT_CONFIGURATION
AC_SUBST(DM_DEFAULT_CONFIGURATION)

AC_SUBST(CRYPTLIB)
AC_SUBST(SIEVEINC)
AC_SUBST(SIEVELIB)
AC_SUBST(SORTALIB)
AC_SUBST(SORTLTLIB)
AC_SUBST(AUTHALIB)
AC_SUBST(AUTHLTLIB)
AC_SUBST(LDAPLIB)
AC_SUBST(LDAPINC)
AC_SUBST(SOCKETLIB)
AC_SUBST(DM_LOGDIR)
AC_SUBST(DM_CONFDIR)
AC_SUBST(DM_STATEDIR)
AC_SUBST(DM_PKGLIBDIR)
AC_SUBST(CHECK_LIBS)
AC_SUBST(DM_PWD)
AC_PROG_LIBTOOL

AC_OUTPUT(src/dbmail.h Makefile src/Makefile src/modules/Makefile man/Makefile test/Makefile systemd/Makefile)

DM_MSG_CONFIGURE_RESULTS
