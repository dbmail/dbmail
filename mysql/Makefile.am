if MYSQL
AM_CFLAGS = -I @MYSQLINC@

lib_LIBRARIES = libmysqldbmail.a

libmysqldbmail_a_SOURCES = dbauthmysql.c dbmysql.c dbmsgbufmysql.c \
                           dbsearchmysql.c
endif
