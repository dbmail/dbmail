# $Id$ 

#! /bin/sh


SMTP_OBJECTS = list.o debug.o pipe.o mime.o dbmysql.o misc.o dbmd5.o md5.o
POP_OBJECTS = pop3.o list.o debug.o dbmysql.o dbmd5.o md5.o
CC = cc

MYSQLLIBDIR=/usr/local/lib/mysql

LIBS = -L$(MYSQLLIBDIR)
LIB = -lmysqlclient

# Added the -D_BSD_SOURCE option to suppress warnings
# from compiler about vsyslog function 

CFLAGS = -Wall -ansi -ggdb -D_BSD_SOURCE 

.PHONY: clean install

all: smtp pop3d

smtp: config.h main.h $(SMTP_OBJECTS) main.c
		$(CC)	main.c -o dbmail-smtp $(SMTP_OBJECTS) $(LIBS) $(LIB)

pop3d: pop3.h $(POP_OBJECTS) pop3d.c
		$(CC) pop3d.c -o dbmail-pop3d $(POP_OBJECTS) $(LIBS) $(LIB)

list.o: list.h
debug.o: debug.h
pipe.o: pipe.h
mime.o: mime.h
dbmysql.o:dbmysql.h dbmd5.h
misc.o:misc.h
pop3.o:pop3.h
dbmd5.o:dbmd5.h md5.h

distclean: clean
	rm -rf dbmail-smtp dbmail-pop3d

clean:
	rm -f *.o core
