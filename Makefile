# $Id$ 
# (c) 2000-2001 IC&S, The Netherlands

#! /bin/sh


SMTP_OBJECTS = list.o debug.o pipe.o mime.o dbmysql.o misc.o dbmd5.o md5.o bounce.o forward.o
POP_OBJECTS = pop3.o list.o debug.o dbmysql.o dbmd5.o md5.o mime.o misc.o
IMAP_OBJECTS = imap4.o debug.o dbmysql.o serverservice.o list.o dbmd5.o md5.o imaputil.o \
imapcommands.o mime.o misc.o
DUMP_OBJECTS = debug.o dbmysql.o list.o dbmd5.o md5.o mime.o sstack.o
MAINTENANCE_OBJECTS = debug.o list.o dbmd5.o md5.o dbmysql.o mime.o  
CONFIG_OBJECTS = dbmysql.o list.o md5.o debug.o dbmd5.o mime.o
USER_OBJECTS = debug.o list.o dbmd5.o md5.o dbmysql.o mime.o
CC = cc

MYSQLLIBDIR=/usr/local/lib/mysql

LIBS = -L$(MYSQLLIBDIR)
LIB = -lmysqlclient

# Added the -D_BSD_SOURCE option to suppress warnings
# from compiler about vsyslog function 

CFLAGS = -Wall -ggdb -D_BSD_SOURCE -O2

.PHONY: clean install

all: smtp pop3d maintenance config imapd user

dump: dbmysql.h dumpmsg.c $(DUMP_OBJECTS)
	$(CC) $(CFLAGS) dumpmsg.c -o dumpmsg $(DUMP_OBJECTS) $(LIBS) $(LIB)


smtp: config.h main.h $(SMTP_OBJECTS) main.c
		$(CC)	main.c -o dbmail-smtp $(SMTP_OBJECTS) $(LIBS) $(LIB)

pop3d: pop3.h $(POP_OBJECTS) pop3d.c
		$(CC) pop3d.c -o dbmail-pop3d $(POP_OBJECTS) $(LIBS) $(LIB)

imapd: imap4.h $(IMAP_OBJECTS) imapd.c
	$(CC) $(CFLAGS) imapd.c -o dbmail-imapd $(IMAP_OBJECTS) $(LIBS) $(LIB)

maintenance: maintenance.h $(MAINTENANCE_OBJECTS) maintenance.c
	$(CC) maintenance.c -o dbmail-maintenance $(MAINTENANCE_OBJECTS) $(LIBS) $(LIB)

config: $(CONFIG_OBJECTS) settings.c
	$(CC) settings.c -o dbmail-config $(CONFIG_OBJECTS) $(LIBS) $(LIB)

user: user.h $(MAINTENANCE_OBJECTS) user.c
	$(CC) user.c -o dbmail-adduser $(MAINTENANCE_OBJECTS) $(LIBS) $(LIB)

dumpmsg.o: dbmysql.h
list.o: list.h
debug.o: debug.h
pipe.o: pipe.h config.h
forward.o: forward.h config.h
mime.o: mime.h config.h
dbmysql.o:dbmysql.h dbmd5.h config.h mime.h list.h
misc.o:misc.h config.h
pop3.o:pop3.h config.h
dbmd5.o:dbmd5.h md5.h
bounce.o:bounce.h list.h
imap4.o: imap4.h dbmysql.h debug.h serverservice.h imaputil.h imapcommands.h
imaputil.o: imaputil.h
imapcommands.o: imapcommands.h imaputil.h
serverservice.o: serverservice.h debug.h
maintenance.o: maintenance.h
settings.o: settings.h
user.o: user.h

distclean: clean
	rm -rf dbmail-smtp dbmail-pop3d dbmail-maintenance dbmail-imapd dbmail-config dbmail-adduser

clean:
	rm -f *.o core
