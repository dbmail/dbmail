# $Id$ 
# (c) 2000-2001 IC&S, The Netherlands

#! /bin/sh

DBASETYPE = pgsql
DBASE_AUTHTYPE = pgsql

AUTHOBJECT = $(DBASETYPE)/dbauth$(DBASE_AUTHTYPE).o
MSGBUFOBJECT = $(DBASETYPE)/dbmsgbuf$(DBASETYPE).o
SEARCHOBJECT = $(DBASETYPE)/dbsearch$(DBASETYPE).o
DBOBJECT = $(DBASETYPE)/db$(DBASETYPE).o

SMTP_OBJECTS = list.o debug.o pipe.o mime.o $(DBOBJECT) dbmd5.o md5.o bounce.o forward.o memblock.o \
$(AUTHOBJECT) config.o
INJECTOR_OBJECTS = list.o debug.o $(DBOBJECT) dbmd5.o md5.o $(AUTHOBJECT) mime.o config.o
UNIONE_OBJECTS = list.o debug.o $(DBOBJECT) dbmd5.o md5.o $(AUTHOBJECT) mime.o config.o
MINI_OBJECTS = debug.o $(DBOBJECT) list.o dbmd5.o md5.o $(AUTHOBJECT) mime.o config.o
POP_OBJECTS = pop3.o server.o serverchild.o list.o debug.o $(DBOBJECT) dbmd5.o md5.o mime.o misc.o memblock.o $(AUTHOBJECT) config.o
IMAP_OBJECTS = imap4.o debug.o $(DBOBJECT) server.o serverchild.o list.o dbmd5.o md5.o imaputil.o \
imapcommands.o mime.o misc.o memblock.o rfcmsg.o $(MSGBUFOBJECT) $(SEARCHOBJECT) $(AUTHOBJECT) config.o
MAINTENANCE_OBJECTS = debug.o list.o dbmd5.o md5.o $(DBOBJECT) mime.o memblock.o $(AUTHOBJECT) config.o
CONFIG_OBJECTS = $(DBOBJECT) list.o md5.o debug.o dbmd5.o mime.o memblock.o $(AUTHOBJECT) config.o
USER_OBJECTS = debug.o list.o dbmd5.o md5.o $(DBOBJECT) mime.o memblock.o $(AUTHOBJECT) config.o
VUTCONV_OBJECTS = debug.o list.o dbmd5.o md5.o mime.o $(DBOBJECT) $(AUTHOBJECT) config.o
DBTEST_OBJECTS = debug.o list.o dbmd5.o md5.o mime.o $(DBOBJECT) $(AUTHOBJECT) config.o
CC = cc

PGSQLLIBDIR=/usr/local/pgsql/lib

LIBS = -L$(PGSQLLIBDIR)
LIB = -lpq -lcrypto -lssl

# Added the -D_BSD_SOURCE option to suppress warnings
# from compiler about vsyslog function 
# Added the -D_SVID_SOURCE option because ipc.h asked me to.

CFLAGS = -Wall -O2 -D_BSD_SOURCE -D_SVID_SOURCE

.PHONY: clean install

all: smtp pop3d maintenance imapd user

smtp: config.h main.h $(SMTP_OBJECTS) main.c
		$(CC)	$(CFLAGS) main.c -o dbmail-smtp $(SMTP_OBJECTS) $(LIBS) $(LIB)

pop3d: pop3.h $(POP_OBJECTS) pop3d.c
		$(CC) $(CFLAGS) pop3d.c -o dbmail-pop3d $(POP_OBJECTS) $(LIBS) $(LIB)

imapd: imap4.h $(IMAP_OBJECTS) imapd.c
	$(CC) $(CFLAGS) imapd.c -o dbmail-imapd $(IMAP_OBJECTS) $(LIBS) $(LIB)

maintenance: maintenance.h $(MAINTENANCE_OBJECTS) maintenance.c
	$(CC) $(CFLAGS) maintenance.c -o dbmail-maintenance $(MAINTENANCE_OBJECTS) $(LIBS) $(LIB)

config: $(CONFIG_OBJECTS) settings.c
	$(CC) $(CFLAGS) settings.c -o dbmail-config $(CONFIG_OBJECTS) $(LIBS) $(LIB)

user: user.h $(USER_OBJECTS) user.c
	$(CC) $(CFLAGS) user.c -o dbmail-adduser $(USER_OBJECTS) $(LIBS) $(LIB)

readvut: db.h auth.h vut2dbmail.c $(VUTCONV_OBJECTS)
	$(CC) $(CFLAGS) vut2dbmail.c -o dbmail-readvut $(VUTCONV_OBJECTS) $(LIBS) $(LIB)

injector: db.h auth.h $(INJECTOR_OBJECTS) injector.c
	$(CC) $(CFLAGS) injector.c -o dbmail-smtp-injector $(INJECTOR_OBJECTS) $(LIBS) $(LIB)

unione: db.h auth.h $(INJECTOR_OBJECTS) uni-one-convert.c
	$(CC) $(CFLAGS) uni-one-convert.c -o uni-one-convertor $(UNIONE_OBJECTS) $(LIBS) $(LIB)

raw: db.h auth.h $(INJECTOR_OBJECTS) raw-convert.c
	$(CC) $(CFLAGS) raw-convert.c -o raw-convertor $(UNIONE_OBJECTS) $(LIBS) $(LIB)

miniinjector: db.h $(MINI_OBJECTS) mini-injector.c
	$(CC) $(CFLAGS) mini-injector.c -o dbmail-mini-injector $(MINI_OBJECTS) $(LIBS) $(LIB)

mbox2dbmail:	

dbtest: $(DBTEST_OBJECTS) dbtest.c db.h
	$(CC) $(CFLAGS) dbtest.c -o dbtest $(DBTEST_OBJECTS) $(LIBS) $(LIB)

list.o: list.h debug.h
debug.o: debug.h
pipe.o: pipe.h config.h debug.h
forward.o: forward.h config.h debug.h
mime.o: mime.h config.h debug.h
misc.o:misc.h config.h debug.h
pop3.o:pop3.h config.h debug.h dbmailtypes.h
dbmd5.o:dbmd5.h md5.h debug.h
bounce.o:bounce.h list.h debug.h config.h
imap4.o: imap4.h db.h debug.h imaputil.h imapcommands.h
imaputil.o: imaputil.h db.h memblock.h debug.h dbmailtypes.h
imapcommands.o: imapcommands.h imaputil.h imap4.h db.h memblock.h debug.h dbmailtypes.h
server.o: server.h debug.h list.h serverchild.h config.h
serverchild.o: serverchild.h debug.h config.h list.h
maintenance.o: maintenance.h debug.h
settings.o: settings.h debug.h
user.o: user.h debug.h
memblock.o: memblock.h debug.h
rfcmsg.o: rfcmsg.h dbmailtypes.h
vut2dbmail.o: db.h auth.h
$(DBOBJECT):db.h dbmd5.h config.h mime.h list.h memblock.h debug.h dbmailtypes.h auth.h
$(MSGBUFOBJECT): dbmsgbuf.h db.h
$(SEARCHOBJECT): dbsearch.h db.h
$(AUTHOBJECT): auth.h db.h

distclean: clean
	rm -rf dbmail-smtp dbmail-pop3d dbmail-maintenance dbmail-imapd dbmail-config dbmail-adduser dbmail-readvut mbox2dbmail dbmail-realsmtp

clean:
	rm -f *.o core $(DBASETYPE)/*.o
