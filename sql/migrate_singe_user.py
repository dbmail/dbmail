#!/usr/bin/env python

# dbmail database migration script. Use this script to make the transition
# from DBMail 1.x to DBMail 2.x

import sys

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
    dbmail_1_x['name'] = "dbmail"
if dbmail_1_x['user'] == "":
    dbmail_1_x['user'] = "dbmail"
if dbmail_1_x['pass'] == "":
    dbmail_1_x['pass'] = "Ldiks89M"

print "What Database system does your DBMail 2.0 run on? (type a number)"
dbmail_2_0 = {}
dbmail_2_0['type'] = input("\t1. MySQL\n\t2. PostgreSQL\n")
dbmail_2_0['host'] = raw_input("Database Host:")
dbmail_2_0['name'] = raw_input("name of database:")
dbmail_2_0['user'] = raw_input("DB user:")
dbmail_2_0['pass'] = raw_input("DB password:")
if dbmail_2_0['host'] == "":
    dbmail_2_0['host'] = "tsunami.fastxs.net"
if dbmail_2_0['name'] == "":
    dbmail_2_0['name'] = "dbmail2"
if dbmail_2_0['user'] == "":
    dbmail_2_0['user'] = "dbmail"
if dbmail_2_0['pass'] == "":
    dbmail_2_0['pass'] = "pass"

print "\nLOADING DATABASE DRIVERS\n"
if dbmail_1_x['type'] == 1 or dbmail_2_0['type'] == 1:
    print "loading MySQL driver"
    import MySQLdb
if dbmail_1_x['type'] == 2 or dbmail_2_0['type'] == 2:
    print "loading MySQL driver"
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

# first find the user_idnr to copy information for
user_name = raw_input("Name of user to copy data for?")

print "finding user in database"
cursor_1.execute("""SELECT user_idnr, passwd, client_idnr, maxmail_size, encryption_type
                    FROM users WHERE userid = %s""",
                 (user_name))
record = cursor_1.fetchone()
if record == None:
    print "no such user found.. exiting"
    sys.exit(-1)
orig_user_nr = record[0]
    
cursor_2.execute("""INSERT INTO users (userid, passwd, client_idnr, maxmail_size,
                    encryption_type) VALUES (%s, %s, %s, %s, %s)
                 """, (user_name, record[1], record[2], record[3], record[4]))
cursor_2.execute("""SELECT MAX(user_idnr) FROM users""")
user_record = cursor_2.fetchone()
if user_record == None:
    sys.exit(-1)
new_user_id = user_record[0]

print new_user_id, "=nieuwe user"
    

# copy all records from the aliases table, this does not copy chained aliases!
    
print "copying aliases table"
cursor_1.execute("""SELECT alias, client_idnr
                    FROM aliases WHERE deliver_to = %s""", (orig_user_nr))
i = 0
while 1:
    record = cursor_1.fetchone()
    if record == None:
        break
    cursor_2.execute("""INSERT INTO aliases (alias,
                        deliver_to, client_idnr) VALUES
                        (%s, %s, %s)""", (record[0], new_user_id, record[1]))
    i = i + 1
print "copied %d records from aliases table" % (i)
                     
# mailboxes
print "copying mailboxes, messages and messageblocks table"
nr_mailboxes = 0
cursor_1.execute("""SELECT mailbox_idnr, name, seen_flag,
                    answered_flag, deleted_flag, flagged_flag, recent_flag,
                    draft_flag, no_inferiors, no_select, permission,
                    is_subscribed FROM mailboxes WHERE owner_idnr = %s""",
                 (orig_user_nr))
while 1:
    mailbox_record = cursor_1.fetchone()
    if mailbox_record == None:
        break
    orig_mailbox_id = mailbox_record[0]
    mailbox_name = mailbox_record[1]
    cursor_2.execute("""INSERT INTO mailboxes
                        (owner_idnr, name, seen_flag,
                        answered_flag, deleted_flag, flagged_flag, recent_flag,
                        draft_flag, no_inferiors, no_select, permission,
                        is_subscribed) 
                        VALUES (%s, %s, %s, %s, %s, %s,
                        %s,%s,%s,%s,%s)""", (new_user_id, mailbox_name, mailbox_record[2],
                                                mailbox_record[3],
                                                mailbox_record[4], mailbox_record[5],
                                                mailbox_record[6], mailbox_record[7],
                                                mailbox_record[8], mailbox_record[9],
                                                mailbox_record[10]))
    
    cursor_2.execute("""SELECT mailbox_idnr FROM mailboxes WHERE name = %s AND
                        owner_idnr = %s""", (mailbox_name, new_user_id))
    tmp_record = cursor_2.fetchone()
    if tmp_record == None:
        break
    new_mailbox_id = tmp_record[0]
    if mailbox_record[11] == '1':
        cursor2.execute("""INSERT INTO subscription (user_id, mailbox_id) VALUES (%s, %s)""", (new_user_id, new_mailbox_id))
                        
    nr_mailboxes += 1
    print "now get all messages in mailbox %s, with id %s" % (mailbox_name, orig_mailbox_id)
    cursor_message_source = conn_1.cursor()
    cursor_message_source.execute("""SELECT message_idnr, messagesize, rfcsize, internal_date, seen_flag,
                                     answered_flag, deleted_flag, flagged_flag,
                                     recent_flag, draft_flag, unique_id, status
                                     FROM messages WHERE mailbox_idnr = %s
                                  """, (orig_mailbox_id))
    while 1:
        message_record = cursor_message_source.fetchone()
        if message_record == None:
            break
        # first insert physmessage
        orig_message_id = message_record[0]
        cursor_2.execute("""INSERT into physmessage (messagesize, rfcsize, internal_date)
                            VALUES (%s, %s, %s)""", (message_record[1], message_record[2], message_record[3]))
        cursor_2.execute("""SELECT MAX(id) from physmessage""")
        tmp2_record = cursor_2.fetchone()
        if tmp2_record == None:
            break
        physmessage_id = tmp2_record[0]
        # now insert the message record
        cursor_2.execute("""INSERT INTO messages (mailbox_idnr, physmessage_id, seen_flag,
                            answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag,
                            unique_id, status) VALUES (%s, %s, %s, %s, %s ,%s,
                            %s, %s, %s, %s)""", (new_mailbox_id, physmessage_id,
                                                 message_record[4], message_record[5],
                                                 message_record[6], message_record[7],
                                                 message_record[8], message_record[9],
                                                 message_record[10], message_record[11]))
        cursor_2.execute("""SELECT MAX(message_idnr) FROM messages""")
        # now get all messageblks for this message
        cursor_msgblk_source = conn_1.cursor()
        cursor_msgblk_source.execute("""SELECT messageblk, blocksize FROM messageblks
                                        WHERE message_idnr = %s""", (orig_message_id))
        while 1:
            block_record = cursor_msgblk_source.fetchone()
            if block_record == None:
                break
            # insert messageblk
            cursor_2.execute("""INSERT into messageblks (physmessage_id, messageblk,
                                blocksize) VALUES (%s, %s, %s)
                             """, (physmessage_id, block_record[0], block_record[1]))

        cursor_msgblk_source.close()
    cursor_message_source.close()
    

      
cursor_2.execute("""SELECT SUM(pm.messagesize) FROM mailboxes mbx,
                        messages msg, physmessage pm
                        WHERE pm.id = msg.physmessage_id
                        AND msg.mailbox_idnr = mbx.mailbox_idnr
                        AND mbx.owner_idnr = %s
                        AND msg.status < '2'""", (new_user_id))
size = cursor_2.fetchone()[0]
if size == None:
    size = 0L
          
print "user %ld, size %ld" % (new_user_id,size)
    
cursor_2.execute("""UPDATE users SET curmail_size = %s WHERE
                    user_idnr = %s""", (size, new_user_id))
    
        
# don't forget to update the curmail..
cursor_1.close()
cursor_2.close()
conn_1.close()
conn_2.close()




