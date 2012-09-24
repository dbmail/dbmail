
if WITHCHECK

AM_CFLAGS = @SIEVEINC@ @LDAPINC@
AM_LDFLAGS = -Wl,-u,Cram_getUsername 

INCLUDES = -I$(top_srcdir)/src

if SHARED
STATIC_MODULES=
else
STATIC_MODULES=$(top_srcdir)/src/@SORTLTLIB@ $(top_srcdir)/src/@AUTHLTLIB@ 
endif

CHECK_LDADD= $(STATIC_MODULES) $(top_srcdir)/src/libdbmail.la @CHECK_LIBS@

TESTS=check_dbmail_common \
	check_dbmail_server \
	check_dbmail_deliver \
	check_dbmail_imapd \
	check_dbmail_message \
	check_dbmail_mailbox \
	check_dbmail_mailboxstate \
	check_dbmail_auth \
	check_dbmail_misc \
	check_dbmail_list \
	check_dbmail_memblock \
	check_dbmail_cache \
	check_dbmail_user \
	check_dbmail_util \
	check_dbmail_dsn \
	check_dbmail_capa \
	check_dbmail_sset \
	check_dbmail_db

IMAPD = $(top_srcdir)/src/dm_quota.c \
        $(top_srcdir)/src/dm_cache.c \
        $(top_srcdir)/src/dm_memblock.c \
        $(top_srcdir)/src/imap4.c \
        $(top_srcdir)/src/imapcommands.c \
        $(top_srcdir)/src/dbmail-imapsession.c
	
noinst_PROGRAMS=$(TESTS)

check_dbmail_user_SOURCES=check_dbmail_user.c
check_dbmail_user_LDADD=$(CHECK_LDADD)
check_dbmail_user_INCLUDES=@CHECK_CFLAGS@

check_dbmail_auth_SOURCES=check_dbmail_auth.c
check_dbmail_auth_LDADD=$(CHECK_LDADD)
check_dbmail_auth_INCLUDES=@CHECK_CFLAGS@

check_dbmail_misc_SOURCES=check_dbmail_misc.c
check_dbmail_misc_LDADD=$(CHECK_LDADD)
check_dbmail_misc_INCLUDES=@CHECK_CFLAGS@

check_dbmail_list_SOURCES=check_dbmail_list.c
check_dbmail_list_LDADD=$(CHECK_LDADD)
check_dbmail_list_INCLUDES=@CHECK_CFLAGS@

check_dbmail_memblock_SOURCES=check_dbmail_memblock.c $(top_srcdir)/src/dm_memblock.c
check_dbmail_memblock_LDADD=$(CHECK_LDADD)
check_dbmail_memblock_INCLUDES=@CHECK_CFLAGS@

check_dbmail_cache_SOURCES=check_dbmail_cache.c $(top_srcdir)/src/dm_cache.c
check_dbmail_cache_LDADD=$(CHECK_LDADD)
check_dbmail_cache_INCLUDES=@CHECK_CFLAGS@

check_dbmail_util_SOURCES=check_dbmail_util.c
check_dbmail_util_LDADD=$(CHECK_LDADD)
check_dbmail_util_INCLUDES=@CHECK_CFLAGS@

check_dbmail_dsn_SOURCES=check_dbmail_dsn.c
check_dbmail_dsn_LDADD=$(CHECK_LDADD)
check_dbmail_dsn_INCLUDES=@CHECK_CFLAGS@

check_dbmail_db_SOURCES=check_dbmail_db.c
check_dbmail_db_LDADD=$(CHECK_LDADD)
check_dbmail_db_INCLUDES=@CHECK_CFLAGS@

check_dbmail_common_SOURCES=check_dbmail_common.c
check_dbmail_common_LDADD=$(CHECK_LDADD)
check_dbmail_common_INCLUDES=@CHECK_CFLAGS@

check_dbmail_server_SOURCES=check_dbmail_server.c
check_dbmail_server_LDADD=$(CHECK_LDADD)
check_dbmail_server_INCLUDES=@CHECK_CFLAGS@

check_dbmail_deliver_SOURCES=check_dbmail_deliver.c
check_dbmail_deliver_LDADD=$(CHECK_LDADD)
check_dbmail_deliver_INCLUDES=@CHECK_CFLAGS@

check_dbmail_message_SOURCES=check_dbmail_message.c
check_dbmail_message_LDADD=$(CHECK_LDADD)
check_dbmail_message_INCLUDES=@CHECK_CFLAGS@

check_dbmail_mailbox_SOURCES=check_dbmail_mailbox.c
check_dbmail_mailbox_LDADD=$(CHECK_LDADD)
check_dbmail_mailbox_INCLUDES=@CHECK_CFLAGS@

check_dbmail_mailboxstate_SOURCES=check_dbmail_mailboxstate.c
check_dbmail_mailboxstate_LDADD=$(CHECK_LDADD)
check_dbmail_mailboxstate_INCLUDES=@CHECK_CFLAGS@

check_dbmail_imapd_SOURCES=$(IMAPD) check_dbmail_imapd.c
check_dbmail_imapd_LDADD=$(CHECK_LDADD)
check_dbmail_imapd_INCLUDES=@CHECK_CFLAGS@

check_dbmail_capa_SOURCES=check_dbmail_capa.c
check_dbmail_capa_LDADD=$(CHECK_LDADD)
check_dbmail_capa_INCLUDES=@CHECK_CFLAGS@

check_dbmail_sset_SOURCES=check_dbmail_sset.c $(top_srcdir)/src/dm_sset.c
check_dbmail_sset_LDADD=$(CHECK_LDADD)
check_dbmail_sset_INCLUDES=@CHECK_CFLAGS@
endif
