#!/usr/bin/env python

# dbmail database migration script. Use this script to make the transition
# from DBMail 1.x to DBMail 2.x

#import sys

print """
welcome to the DBMail 1.x -> 2.x migration script
**************************************************
      
  Copyright (C) 2003 IC & S  dbmail@ic-s.nl

  This program is free software; you can redistribute it and/or 
  modify it under the terms of the GNU General Public License 
  as published by the Free Software Foundation; either 
  version 2 of the License, or (at your option) any later 
  version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
"""

print "***********************************************************"
print "These next questions concern your DBMail 1.x database"
print "What Database system does your DBMail 1.x run on? (type a number)"
dbmail_1_x = {}
dbmail_1_x['type'] = input("\t1. MySQL\n\t2. PostgreSQL\n")
dbmail_1_x['host'] = raw_input("Database Host:")
dbmail_1_x['name'] = raw_input("name of database:")
dbmail_1_x['user'] = raw_input("DB user:")
dbmail_1_x['pass'] = raw_input("DB password:")
if dbmail_1_x['host'] == "":
    dbmail_1_x['host'] = "localhost"
if dbmail_1_x['name'] == "":
    dbmail_1_x['name'] = "dbmail1_1"
if dbmail_1_x['user'] == "":
    dbmail_1_x['user'] = "ilja"
if dbmail_1_x['pass'] == "":
    dbmail_1_x['pass'] = "pass"

print "What Database system does your DBMail 2.0 run on? (type a number)"
dbmail_2_0 = {}
dbmail_2_0['type'] = input("\t1. MySQL\n\t2. PostgreSQL\n")
dbmail_2_0['host'] = raw_input("Database Host:")
dbmail_2_0['name'] = raw_input("name of database:")
dbmail_2_0['user'] = raw_input("DB user:")
dbmail_2_0['pass'] = raw_input("DB password:")
if dbmail_2_0['host'] == "":
    dbmail_2_0['host'] = "localhost"
if dbmail_2_0['name'] == "":
    dbmail_2_0['name'] = "dbmail"
if dbmail_2_0['user'] == "":
    dbmail_2_0['user'] = "ilja"
if dbmail_2_0['pass'] == "":
    dbmail_2_0['pass'] = "pass"

print "\nLOADING DATABASE DRIVERS\n"
if dbmail_1_x['type'] == 1 or dbmail_2_0['type'] == 1:
    print "loading MySQL driver"
    import MySQLdb
if dbmail_1_x['type'] == 2 or dbmail_2_0['type'] == 2:
    print "loading PostgreSQL driver"
    import pyPgSQL.PgSQL as PgSQL

print "connecting to databases"

if dbmail_1_x['type'] == 1:
    conn_1 = MySQLdb.connect(user = dbmail_1_x['user'], db=dbmail_1_x['name'], passwd = dbmail_1_x['pass'], host = dbmail_1_x['host'])
else:
    conn_1 = PgSQL.connect(user = dbmail_1_x['user'], database=dbmail_1_x['name'], password = dbmail_1_x['pass'], host = dbmail_1_x['host'])
if dbmail_2_0['type'] == 1:
    conn_2 = MySQLdb.connect(user = dbmail_2_0['user'], db=dbmail_2_0['name'], passwd = dbmail_2_0['pass'], host = dbmail_2_0['host'])
else:
    conn_2 = PgSQL.connect(user = dbmail_2_0['user'], databaseb=dbmail_2_0['name'], password = dbmail_2_0['pass'], host = dbmail_2_0['host'])
# get two database cursors, one for each database
cursor_1 = conn_1.cursor()
cursor_2 = conn_2.cursor()

# copy all records from the aliases table

print "copying aliases table"
cursor_1.execute("""SELECT alias_idnr, alias, deliver_to, client_idnr
                    FROM aliases""")
cursor_2.execute("""DELETE FROM aliases""")
i = 0
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO aliases (alias_idnr, alias,
                        deliver_to, client_idnr) VALUES
                        (%s, %s, %s, %s)""", record)
    i += 1
print "copied %d records from aliases table" % (i)

# auto_notifications
print "copying auto_notifications table"
i = 0
cursor_1.execute("""SELECT auto_notify_idnr, user_idnr, notify_address
                    FROM auto_notifications""")
cursor_2.execute("""DELETE FROM auto_notifications""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO auto_notifications
                        (auto_notify_idnr, user_idnr, notify_address)
                        VALUES (%s, %s, %s)""", record)
    i += 1
print "copied %d records from auto_notifications table" % (i)
# auto_replies
print "copying auto_replies table"
i = 0
cursor_1.execute("""SELECT auto_reply_idnr, user_idnr, reply_body
                    FROM auto_replies""")
cursor_2.execute("""DELETE FROM auto_replies""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO auto_replies
                        (auto_reply_idnr, user_idnr, reply_body)
                        VALUES (%s, %s, %s)""", record)
    i += 1
print "copied %d records from auto_reply table" % (i)
# config table
print "copying config table"
i = 0
cursor_1.execute("""SELECT configid, item, value
                    FROM config""")
cursor_2.execute("""DELETE FROM config""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO config
                        (configid, item, value)
                        VALUES (%s, %s, %s)""", record)
    i += 1
print "copied %d records from config table" % (i)
# mailboxes
print "copying mailboxes table"
i = 0
cursor_1.execute("""SELECT mailbox_idnr, owner_idnr, name, seen_flag,
                    answered_flag, deleted_flag, flagged_flag, recent_flag,
                    draft_flag, no_inferiors, no_select, permission,
                    is_subscribed FROM mailboxes""")
cursor_2.execute("""DELETE FROM mailboxes""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO mailboxes
                        (mailbox_idnr, owner_idnr, name, seen_flag,
                        answered_flag, deleted_flag, flagged_flag, recent_flag,
                        draft_flag, no_inferiors, no_select, permission,
                        is_subscribed) 
                        VALUES (%s, %s, %s, %s, %s, %s, %s,
                        %s,%s,%s,%s,%s,%s)""", record)
    i += 1
print "copied %d records from mailboxes table" % (i)

# messages
# this table cannot be simply copied. We need to split each record
# between the messages table and the physmessage table
i = 0
print "copy messages table to messages and physmessage table"
cursor_1.execute("""SELECT messagesize, rfcsize, internal_date,
                    message_idnr, mailbox_idnr, seen_flag, answered_flag,
                    deleted_flag, flagged_flag, recent_flag, draft_flag,
                    unique_id, status FROM messages""")
cursor_2.execute("""DELETE FROM messages""")
cursor_2.execute("""DELETE FROM physmessage""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO physmessage (messagesize,
                        rfcsize, internal_date) VALUES
                        (%s, %s, %s)""", (record[0], record[1], record[2]))
    new_id = cursor_2.lastrowid
    cursor_2.execute("""INSERT INTO messages (physmessage_id, message_idnr,
                        mailbox_idnr, seen_flag, answered_flag, deleted_flag,
                        flagged_flag, recent_flag, draft_flag, unique_id, status)
                        VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)""",
                     (new_id, record[3], record[4], record[5], record[6], record[7],
                      record[8], record[9], record[10], record[11], record[12]))
    i += 1
print "copied %d records of messages into messages and physmessage" % i

# messageblks table
# most of the information from this table can be copied. Only the message_idnr
# from the original table has to be replaced by the appropriate physmessage_id
i = 0
print "copying records from messageblks table"
cursor_1.execute("""SELECT messageblk_idnr, message_idnr, messageblk, blocksize
                    FROM messageblks""")
cursor_2.execute("""DELETE FROM messageblks""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""SELECT physmessage_id FROM messages WHERE
                        message_idnr = %s""", (record[1]))
    phys_id_record = cursor_2.fetchone()
    if phys_id_record == None:
        print("Oops. Inconsistency in database. run dbmail-maintenance (version 1.x). exiting..")
        exit(-1)
    cursor_2.execute("""INSERT INTO messageblks (messageblk_idnr, physmessage_id,
                        messageblk, blocksize) VALUES (%s, %s, %s, %s)""",
                     (record[0], phys_id_record[0], record[2], record[3]))
    i += 1
print "copied %d records of messageblks into messablks" % i

# users table
i = 0
print "copying records from users table"
cursor_1.execute("""SELECT user_idnr, userid, passwd, client_idnr, maxmail_size,
                    encryption_type, last_login FROM users""")
cursor_2.execute("""DELETE FROM users""")
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO users (user_idnr, userid, passwd, client_idnr,
                        maxmail_size, encryption_type, last_login) VALUES
                        (%s, %s, %s, %s, %s, %s ,%s)""", (record))
    cursor_2.execute("""SELECT SUM(pm.messagesize) FROM mailboxes mbx,
                        messages msg, physmessage pm
                        WHERE pm.id = msg.physmessage_id
                        AND msg.mailbox_idnr = mbx.mailbox_idnr
                        AND mbx.owner_idnr = %s
                        AND msg.status < '2'""", (record[0]))
    size = cursor_2.fetchone()[0]
    if size == None:
        size = 0L
          
    print "user %ld, size %ld" % (record[0],size)
    
    cursor_2.execute("""UPDATE users SET curmail_size = %s WHERE
                        user_idnr = %s""", (size, record[0]))
    
        
# don't forget to update the curmail..
cursor_1.close()
cursor_2.close()
conn_1.close()
conn_2.close()




