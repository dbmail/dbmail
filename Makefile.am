SUBDIRS = mysql pgsql .

bin_PROGRAMS = dbmail-smtp dbmail-pop3d dbmail-imapd dbmail-maintenance dbmail-config dbmail-adduser dbmail-readvut dbmail-smtp-injector uni-one-convertor raw-convertor dbmail-mini-injector 
bin_SCRIPTS = install-dbmail.sh

dbmail_smtp_SOURCES = list.c debug.c dbmd5.c md5.c mime.c main.c pipe.c bounce.c forward.c memblock.c
dbmail_smtp_LDADD = @SQLALIB@
dbmail_smtp_LDFLAGS = @SQLLIB@

dbmail_pop3d_SOURCES = pop3.c list.c debug.c dbmd5.c md5.c mime.c misc.c memblock.c pop3d.c
dbmail_pop3d_LDADD = @SQLALIB@
dbmail_pop3d_LDFLAGS = @SQLLIB@

dbmail_imapd_SOURCES = imap4.c debug.c serverservice.c list.c dbmd5.c md5.c imaputil.c imapcommands.c mime.c misc.c memblock.c rfcmsg.c imapd.c
dbmail_imapd_LDADD = @SQLALIB@
dbmail_imapd_LDFLAGS = @SQLLIB@

dbmail_maintenance_SOURCES = debug.c list.c dbmd5.c md5.c mime.c memblock.c maintenance.c auth.h
dbmail_maintenance_LDADD = @SQLALIB@
dbmail_maintenance_LDFLAGS = @SQLLIB@

dbmail_config_SOURCES = list.c md5.c debug.c dbmd5.c mime.c memblock.c settings.c
dbmail_config_LDADD = @SQLALIB@
dbmail_config_LDFLAGS = @SQLLIB@

dbmail_adduser_SOURCES = debug.c list.c dbmd5.c md5.c mime.c memblock.c user.c
dbmail_adduser_LDADD = @SQLALIB@
dbmail_adduser_LDFLAGS = @SQLLIB@

dbmail_readvut_SOURCES = debug.c list.c dbmd5.c md5.c mime.c vut2dbmail.c
dbmail_readvut_LDADD = @SQLALIB@
dbmail_readvut_LDFLAGS = @SQLLIB@

#dbmail_realsmtp_SOURCES = debug.c dbmd5.c md5.c list.c mime.c \
#                          serverservice.c misc.c memblock.c
#dbmail_realsmtp_LDADD = @SQLALIB@
#dbmail_realsmtp_LDFLAGS = @SQLLIB@

dbmail_smtp_injector_SOURCES = list.c debug.c dbmd5.c md5.c mime.c injector.c
dbmail_smtp_injector_LDADD = @SQLALIB@
dbmail_smtp_injector_LDFLAGS = @SQLLIB@

uni_one_convertor_SOURCES = list.c debug.c dbmd5.c md5.c mime.c uni-one-convert.c
uni_one_convertor_LDADD = @SQLALIB@
uni_one_convertor_LDFLAGS = @SQLLIB@

raw_convertor_SOURCES = list.c debug.c dbmd5.c md5.c mime.c raw-convert.c
raw_convertor_LDADD = @SQLALIB@
raw_convertor_LDFLAGS = @SQLLIB@

dbmail_mini_injector_SOURCES = debug.c list.c dbmd5.c md5.c mime.c mini-injector.c
dbmail_mini_injector_LDADD = @SQLALIB@
dbmail_mini_injector_LDFLAGS = @SQLLIB@

