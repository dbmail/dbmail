-- SQL for upgrading from dbmail-1.2 to dbmail-2.0
/*
 Copyright (C) 2003-2004 Paul J Stevens paul@nfg.nl

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
*/

-- $Id$

/* first start a transaction to possibly create the auto_replies and 
   auto_notifications tables, which might, or might not be present.
*/
BEGIN TRANSACTION;

CREATE SEQUENCE auto_notification_seq;
CREATE TABLE auto_notifications (
   auto_notify_idnr INT8 DEFAULT nextval('auto_notification_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   notify_address VARCHAR(100),
   PRIMARY KEY (auto_notify_idnr),
   FOREIGN KEY (user_idnr)
	REFERENCES users (user_idnr) ON DELETE CASCADE
);

CREATE SEQUENCE auto_reply_seq;
CREATE TABLE auto_replies (
   auto_reply_idnr INT8 DEFAULT nextval('auto_reply_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   reply_body TEXT,
   PRIMARY KEY(auto_reply_idnr),
   FOREIGN KEY (user_idnr)
	REFERENCES users (user_idnr) ON DELETE CASCADE
);

COMMIT;

/* the next transaction will add an index to the auto_replies 
   and auto_notifications tables that was not present in
   DBMail 1.2.x
*/
BEGIN TRANSACTION;
CREATE INDEX auto_notifications_user_idnr_idx ON 
	auto_notifications(user_idnr);
CREATE INDEX auto_replies_user_idnr_idx ON 
	auto_replies(user_idnr);
COMMIT;

/* Now begin the real work. This might take awhile.. */

BEGIN TRANSACTION;

-- alter the aliases table
DROP INDEX aliases_alias_idx;
DROP INDEX aliases_alias_low_idx;
ALTER TABLE alias_idnr_seq RENAME TO dbmail_alias_idnr_seq;
ALTER TABLE aliases RENAME TO dbmail_aliases;
ALTER TABLE dbmail_aliases ALTER COLUMN alias_idnr 
	SET DEFAULT nextval('dbmail_alias_idnr_seq');
CREATE INDEX dbmail_aliases_alias_idx ON dbmail_aliases(alias);
CREATE INDEX dbmail_aliases_alias_low_idx ON dbmail_aliases(lower(alias));

-- alter the users table.
DROP INDEX users_name_idx;
ALTER TABLE user_idnr_seq RENAME TO dbmail_user_idnr_seq;
ALTER TABLE users RENAME TO dbmail_users;
ALTER TABLE dbmail_users ALTER COLUMN user_idnr
	SET DEFAULT nextval('dbmail_user_idnr_seq');
ALTER TABLE dbmail_users ADD COLUMN curmail_size INT8;
ALTER TABLE dbmail_users ALTER COLUMN curmail_size SET DEFAULT '0';
UPDATE dbmail_users SET curmail_size = '0';
ALTER TABLE dbmail_users ALTER COLUMN curmail_size SET NOT NULL;
CREATE UNIQUE INDEX dbmail_users_name_idx ON dbmail_users(userid);

-- alter the mailboxes table
DROP INDEX mailboxes_id_idx;
DROP INDEX mailboxes_owner_idx;
DROP INDEX mailboxes_name_idx;
DROP INDEX mailboxes_is_subscribed_idx;
ALTER TABLE mailbox_idnr_seq RENAME TO dbmail_mailbox_idnr_seq;
ALTER TABLE mailboxes RENAME TO dbmail_mailboxes;
ALTER TABLE dbmail_mailboxes ALTER COLUMN mailbox_idnr
	SET DEFAULT nextval('dbmail_mailbox_idnr_seq');
CREATE INDEX dbmail_mailboxes_owner_idx ON dbmail_mailboxes(owner_idnr);
CREATE INDEX dbmail_mailboxes_name_idx ON dbmail_mailboxes(name);
CREATE INDEX dbmail_mailboxes_owner_name_idx 
	ON dbmail_mailboxes(owner_idnr, name);

-- create the subscription table.
CREATE TABLE dbmail_subscription (
   user_id INT8 NOT NULL,
   mailbox_id INT8 NOT NULL,
   PRIMARY KEY (user_id, mailbox_id)
);

-- the dbmail_subscription table can now be filled with values from the
-- dbmail_mailboxes table
INSERT INTO dbmail_subscription (user_id, mailbox_id) 
SELECT owner_idnr, mailbox_idnr FROM dbmail_mailboxes
WHERE is_subscribed = '1';

-- no add the foreign key relations to the dbmail_subscription table
ALTER TABLE dbmail_subscription ADD FOREIGN KEY (user_id) 
	REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE;
ALTER TABLE dbmail_subscription ADD FOREIGN KEY (mailbox_id) 
	REFERENCES dbmail_mailboxes(mailbox_idnr) ON DELETE CASCADE;

-- The is_subscribed column can now be dropped from the dbmail_mailboxes
-- table.
ALTER TABLE dbmail_mailboxes DROP COLUMN is_subscribed;

-- the dbmail_acl table is completely new in 2.0
CREATE TABLE dbmail_acl (
    user_id INT8 NOT NULL,
    mailbox_id INT8 NOT NULL,
    lookup_flag INT2 DEFAULT '0' NOT NULL,
    read_flag INT2 DEFAULT '0' NOT NULL,
    seen_flag INT2 DEFAULT '0' NOT NULL,
    write_flag INT2 DEFAULT '0' NOT NULL,
    insert_flag INT2 DEFAULT '0' NOT NULL,
    post_flag INT2 DEFAULT '0' NOT NULL,
    create_flag INT2 DEFAULT '0' NOT NULL,
    delete_flag INT2 DEFAULT '0' NOT NULL,
    administer_flag INT2 DEFAULT '0' NOT NULL,
    PRIMARY KEY (user_id, mailbox_id),
    FOREIGN KEY (user_id) 
	REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE,
    FOREIGN KEY (mailbox_id) 
	REFERENCES dbmail_mailboxes (mailbox_idnr) ON DELETE CASCADE
);

-- create the physmessage table
CREATE SEQUENCE dbmail_physmessage_id_seq;
CREATE TABLE dbmail_physmessage (
   id INT8 DEFAULT nextval('dbmail_physmessage_id_seq'),
   messagesize INT8 DEFAULT '0' NOT NULL,   
   rfcsize INT8 DEFAULT '0' NOT NULL,
   internal_date TIMESTAMP WITHOUT TIME ZONE,
   PRIMARY KEY(id)
);

-- fill the table from the messages table.
INSERT INTO dbmail_physmessage (id, messagesize, rfcsize, internal_date)
SELECT message_idnr, messagesize, rfcsize, internal_date FROM messages;
-- set the initial value for dbmail_physmessage_id_seq
SELECT setval('dbmail_physmessage_id_seq', max(id)) FROM dbmail_physmessage;

-- alter the messages table
DROP INDEX messages_id_idx;
DROP INDEX messages_mailbox_idx;
DROP INDEX messages_seen_flag_idx;
DROP INDEX messages_unique_id_idx;
DROP INDEX messages_status_idx;
ALTER TABLE message_idnr_seq RENAME TO dbmail_message_idnr_seq;
ALTER TABLE messages RENAME TO dbmail_messages;
ALTER TABLE dbmail_messages ALTER COLUMN message_idnr 
	SET DEFAULT nextval('dbmail_message_idnr_seq');
ALTER TABLE dbmail_messages ALTER COLUMN status
	SET DEFAULT '0';
ALTER TABLE dbmail_messages ADD COLUMN physmessage_id INT8;
ALTER TABLE dbmail_messages ALTER COLUMN physmessage_id
	SET DEFAULT '0';
UPDATE dbmail_messages SET physmessage_id = message_idnr;
ALTER TABLE dbmail_messages ALTER COLUMN physmessage_id
	SET NOT NULL;
ALTER TABLE dbmail_messages ADD FOREIGN KEY (physmessage_id) 
	REFERENCES dbmail_physmessage(id) ON DELETE CASCADE;
ALTER TABLE dbmail_messages DROP COLUMN messagesize;
ALTER TABLE dbmail_messages DROP COLUMN rfcsize;
ALTER TABLE dbmail_messages DROP COLUMN internal_date;
CREATE INDEX dbmail_messages_mailbox_idx ON dbmail_messages(mailbox_idnr);
CREATE INDEX dbmail_messages_physmessage_idx 
	ON dbmail_messages(physmessage_id);
CREATE INDEX dbmail_messages_seen_flag_idx ON dbmail_messages(seen_flag);
CREATE INDEX dbmail_messages_unique_id_idx ON dbmail_messages(unique_id);
CREATE INDEX dbmail_messages_status_idx ON dbmail_messages(status);
CREATE INDEX dbmail_messages_status_notdeleted_idx 
	ON dbmail_messages(status) WHERE status < '2';

-- alter dbmail_messageblks
DROP INDEX messageblks_id_idx;
DROP INDEX messageblks_msg_idx;
ALTER TABLE messageblk_idnr_seq RENAME TO dbmail_messageblk_idnr_seq;
ALTER TABLE messageblks RENAME TO dbmail_messageblks;
ALTER TABLE dbmail_messageblks ALTER COLUMN messageblk_idnr
	SET DEFAULT nextval('dbmail_messageblk_idnr_seq');
ALTER TABLE dbmail_messageblks ADD COLUMN is_header INT2;
ALTER TABLE dbmail_messageblks ALTER COLUMN is_header
	SET DEFAULT '0';
UPDATE dbmail_messageblks SET is_header = '0';
ALTER TABLE dbmail_messageblks ALTER COLUMN is_header
	SET NOT NULL;
ALTER TABLE dbmail_messageblks RENAME COLUMN message_idnr TO physmessage_id;
CREATE INDEX dbmail_messageblks_physmessage_idx ON 
	dbmail_messageblks(physmessage_id);
CREATE INDEX dbmail_messageblks_physmessage_is_header_idx 
	ON dbmail_messageblks(physmessage_id, is_header);
ALTER TABLE dbmail_messageblks ADD FOREIGN KEY (physmessage_id) 
	REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;

-- alter the auto_notifications table
DROP SEQUENCE auto_notification_seq;
ALTER TABLE auto_notifications RENAME TO dbmail_auto_notifications;
ALTER TABLE dbmail_auto_notifications DROP COLUMN auto_notify_idnr;
ALTER TABLE dbmail_auto_notifications ADD FOREIGN KEY (user_idnr)
	REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE;
CREATE INDEX dbmail_auto_notifications_user_index ON
	dbmail_auto_notifications(user_idnr);

-- alter the auto_replies table
DROP SEQUENCE auto_reply_seq;
ALTER TABLE auto_replies RENAME TO dbmail_auto_replies;
ALTER TABLE dbmail_auto_replies DROP COLUMN auto_reply_idnr;
ALTER TABLE dbmail_auto_replies ADD FOREIGN KEY (user_idnr)
	REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE;
CREATE INDEX dbmail_auto_replies_user_index ON
	dbmail_auto_replies(user_idnr);

-- alter the pbsp (pop-before-smtp) table
CREATE SEQUENCE dbmail_pbsp_idnr_seq;
CREATE TABLE dbmail_pbsp (
  idnr INT8 NOT NULL DEFAULT NEXTVAL('dbmail_pbsp_idnr_seq'),
  since TIMESTAMP NOT NULL DEFAULT '1970-01-01 00:00:00',
  ipnumber INET NOT NULL DEFAULT '0.0.0.0',
  PRIMARY KEY (idnr)
);
CREATE UNIQUE INDEX dbmail_idx_ipnumber ON dbmail_pbsp(ipnumber);
CREATE INDEX dbmail_idx_since ON dbmail_pbsp(since);

--- Create the user for the delivery chain:
INSERT INTO dbmail_users (userid, passwd, encryption_type)
	VALUES ('__@!internal_delivery_user!@__', '', 'md5');

-- Commit transaction

COMMIT;

/* the old config table might still be around. This will deleted */
BEGIN TRANSACTION;

DROP TABLE config;

COMMIT;