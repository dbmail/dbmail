# $Id$ 

#! /bin/sh


SMTP_OBJECTS = list.o debug.o pipe.o mime.o dbmysql.o misc.o dbmd5.o md5.o bounce.o
POP_OBJECTS = pop3.o list.o debug.o dbmysql.o dbmd5.o md5.o
IMAP_OBJECTS = imap4.o debug.o dbmysql.o serverservice.o list.o dbmd5.o md5.o imaputil.o imapcommands.o
DUMP_OBJECTS = debug.o dbmysql.o list.o dbmd5.o md5.o 
MAINTENANCE_OBJECTS = debug.o list.o dbmd5.o md5.o dbmysql.o
CC = cc

MYSQLLIBDIR=/usr/local/lib/mysql

LIBS = -L$(MYSQLLIBDIR)
LIB = -lmysqlclient

# Added the -D_BSD_SOURCE option to suppress warnings
# from compiler about vsyslog function 

CFLAGS = -Wall -ggdb -D_BSD_SOURCE 

.PHONY: clean install

all: smtp pop3d imapd

dump: dbmysql.h dumpmsg.c $(DUMP_OBJECTS)
	$(CC) $(CFLAGS) dumpmsg.c -o dumpmsg $(DUMP_OBJECTS) $(LIBS) $(LIB)


smtp: config.h main.h $(SMTP_OBJECTS) main.c
		$(CC)	main.c -o dbmail-smtp $(SMTP_OBJECTS) $(LIBS) $(LIB)

pop3d: pop3.h $(POP_OBJECTS) pop3d.c
		$(CC) pop3d.c -o dbmail-pop3d $(POP_OBJECTS) $(LIBS) $(LIB)

imapd: imap4.h $(IMAP_OBJECTS) imapd.c
	$(CC) imapd.c -o dbmail-imapd $(IMAP_OBJECTS) $(LIBS) $(LIB)

maintenance: maintenance.h $(MAINTENANCE_OBJECTS) maintenance.c
	$(CC) maintenance.c -o dbmail-maintenance $(MAINTENANCE_OBJECTS) $(LIBS) $(LIB)

dumpmsg.o: dbmysql.h
list.o: list.h
debug.o: debug.h
pipe.o: pipe.h config.h
mime.o: mime.h config.h
dbmysql.o:dbmysql.h dbmd5.h config.h
misc.o:misc.h config.h
pop3.o:pop3.h config.h
dbmd5.o:dbmd5.h md5.h
bounce.o:bounce.h list.h
imap4.o: imap4.h dbmysql.h debug.h serverservice.h imaputil.h imapcommands.h
imaputil.o: imaputil.h
imapcommands.o: imapcommands.h imaputil.h
serverservice.o: serverservice.h debug.h
maintenance.o: maintenance.h

distclean: clean
	rm -rf dbmail-smtp dbmail-pop3d dbmail-imap4d dbmail-maintenance

clean:
	rm -f *.o core
