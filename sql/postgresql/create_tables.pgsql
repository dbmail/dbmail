/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
/* $Id$
*/

CREATE SEQUENCE alias_idnr_seq;
CREATE TABLE aliases (
    alias_idnr INT8 DEFAULT nextval('alias_idnr_seq'),
    alias VARCHAR(100) NOT NULL, 
    deliver_to VARCHAR(250) NOT NULL,
    client_idnr INT8 DEFAULT '0' NOT NULL,
    PRIMARY KEY (alias_idnr)
);
CREATE UNIQUE INDEX aliases_alias_idx ON aliases(alias);
CREATE UNIQUE INDEX aliases_alias_low_idx ON aliases(lower(alias));

CREATE SEQUENCE user_idnr_seq;
CREATE TABLE users (
   user_idnr INT8 DEFAULT nextval('user_idnr_seq'),
   userid VARCHAR(100) NOT NULL,
   passwd VARCHAR(34) NOT NULL,
   client_idnr INT8 DEFAULT '0' NOT NULL,
   maxmail_size INT8 DEFAULT '0' NOT NULL,
   curmail_size INT8 DEFAULT '0' NOT NULL,
   encryption_type VARCHAR(20) DEFAULT '' NOT NULL,
   last_login TIMESTAMP DEFAULT '1979-11-03 22:05:58' NOT NULL,
   PRIMARY KEY (user_idnr)
);
CREATE INDEX users_name_idx ON users(userid);

CREATE SEQUENCE mailbox_idnr_seq;
CREATE TABLE mailboxes (
   mailbox_idnr INT8 DEFAULT nextval('mailbox_idnr_seq'),
   owner_idnr INT8 NOT NULL,
   name VARCHAR(100) NOT NULL,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   no_inferiors INT2 DEFAULT '0' NOT NULL,
   no_select INT2 DEFAULT '0' NOT NULL,
   permission INT2 DEFAULT '2',
   is_subscribed INT2 DEFAULT '0' NOT NULL,
   PRIMARY KEY (mailbox_idnr),
   FOREIGN KEY (owner_idnr) REFERENCES users(user_idnr) ON DELETE CASCADE
);
CREATE INDEX mailboxes_owner_idx ON mailboxes(owner_idnr);
CREATE INDEX mailboxes_name_idx ON mailboxes(name);
CREATE INDEX mailboxes_is_subscribed_idx ON mailboxes(is_subscribed);

CREATE SEQUENCE shared_mailbox_seq;
CREATE TABLE shared_mailbox (
       id INT8 DEFAULT nextval('shared_mailbox_seq'),
       mailbox_id INT8 NOT NULL,
       maxmail_size INT8 DEFAULT '0' NOT NULL,
       curmail_size INT8 DEFAULT '0' NOT NULL,
       PRIMARY KEY(id),
       FOREIGN KEY(mailbox_id) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE
);
CREATE INDEX shared_mailbox_mailbox_id_idx ON shared_mailbox(mailbox_id);

CREATE SEQUENCE shared_mailbox_access_seq;
CREATE TABLE shared_mailbox_access (
       id INT8 DEFAULT nextval('shared_mailbox_access_seq'),
       shared_mailbox_id INT8 DEFAULT '0' NOT NULL,
       user_id INT8 DEFAULT '0' NOT NULL,
       lookup_flag INT2 DEFAULT '0' NOT NULL,
       read_flag INT2 DEFAULT '0' NOT NULL,
       insert_flag INT2 DEFAULT '0' NOT NULL,
       write_flag INT2 DEFAULT '0' NOT NULL,
       is_subscribed INT2 DEFAULT '0' NOT NULL,
       PRIMARY KEY (id),
       FOREIGN KEY (shared_mailbox_id) REFERENCES shared_mailbox(id) ON DELETE CASCADE,
       FOREIGN KEY (user_id) REFERENCES users (user_idnr) ON DELETE CASCADE
);
CREATE INDEX shared_mailbox_access_shared_mailbox_id ON shared_mailbox_access(shared_mailbox_id);
CREATE INDEX shared_mailbox_access_user_id_idx ON shared_mailbox_access(user_id);


CREATE SEQUENCE physmessage_id_seq;
CREATE TABLE physmessage (
   id INT8 DEFAULT nextval('physmessage_id_seq'),
   messagesize INT8 DEFAULT '0' NOT NULL,   
   rfcsize INT8 DEFAULT '0' NOT NULL,
   internal_date TIMESTAMP,
   PRIMARY KEY(id)
);

CREATE SEQUENCE message_idnr_seq;
CREATE TABLE messages (
   message_idnr INT8 DEFAULT nextval('message_idnr_seq'),
   mailbox_idnr INT8 DEFAULT '0' NOT NULL,
   physmessage_id INT8 DEFAULT '0' NOT NULL,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   unique_id varchar(70) NOT NULL,
   status INT2 DEFAULT '000' NOT NULL,
   PRIMARY KEY (message_idnr),
   FOREIGN KEY (physmessage_id) REFERENCES physmessage(id) ON DELETE CASCADE,
   FOREIGN KEY (mailbox_idnr) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE
);
CREATE INDEX messages_mailbox_idx ON messages(mailbox_idnr);
CREATE INDEX messages_physmessage_idx ON messages(physmessage_id);
CREATE INDEX messages_seen_flag_idx ON messages(seen_flag);
CREATE INDEX messages_unique_id_idx ON messages(unique_id);
CREATE INDEX messages_status_idx ON messages(status);

CREATE SEQUENCE messageblk_idnr_seq;
CREATE TABLE messageblks (
   messageblk_idnr INT8 DEFAULT nextval('messageblk_idnr_seq'),
   physmessage_id INT8 DEFAULT '0' NOT NULL,
   messageblk TEXT NOT NULL,
   blocksize INT8 DEFAULT '0' NOT NULL,
   PRIMARY KEY (messageblk_idnr),
   FOREIGN KEY (physmessage_id) REFERENCES physmessage (id) ON DELETE CASCADE
);
CREATE INDEX messageblks_physmessage_idx ON messageblks(physmessage_id);


CREATE SEQUENCE auto_notification_seq;
CREATE TABLE auto_notifications (
   auto_notify_idnr INT8 DEFAULT nextval('auto_notification_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   notify_address VARCHAR(100),
   PRIMARY KEY (auto_notify_idnr),
   FOREIGN KEY (user_idnr) REFERENCES users (user_idnr) ON DELETE CASCADE
);
CREATE INDEX auto_notifications_user_idnr_idx ON auto_notifications(user_idnr);


CREATE SEQUENCE auto_reply_seq;
CREATE TABLE auto_replies (
   auto_reply_idnr INT8 DEFAULT nextval('auto_reply_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   reply_body TEXT,
   PRIMARY KEY(auto_reply_idnr),
   FOREIGN KEY (user_idnr) REFERENCES users (user_idnr) ON DELETE CASCADE
);
CREATE INDEX auto_replies_user_idnr_idx ON auto_replies(user_idnr);
