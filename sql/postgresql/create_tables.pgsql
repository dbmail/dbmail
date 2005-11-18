/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
/* $Id: create_tables.pgsql 1909 2005-11-11 10:31:00Z paul $
*/

BEGIN TRANSACTION;

CREATE SEQUENCE dbmail_alias_idnr_seq;
CREATE TABLE dbmail_aliases (
    alias_idnr INT8 DEFAULT nextval('dbmail_alias_idnr_seq'),
    alias VARCHAR(100) NOT NULL, 
    deliver_to VARCHAR(250) NOT NULL,
    client_idnr INT8 DEFAULT '0' NOT NULL,
    PRIMARY KEY (alias_idnr)
);
CREATE INDEX dbmail_aliases_alias_idx ON dbmail_aliases(alias);
CREATE INDEX dbmail_aliases_alias_low_idx ON dbmail_aliases(lower(alias));

CREATE SEQUENCE dbmail_user_idnr_seq;
CREATE TABLE dbmail_users (
   user_idnr INT8 DEFAULT nextval('dbmail_user_idnr_seq'),
   userid VARCHAR(100) NOT NULL,
   passwd VARCHAR(34) NOT NULL,
   client_idnr INT8 DEFAULT '0' NOT NULL,
   maxmail_size INT8 DEFAULT '0' NOT NULL,
   curmail_size INT8 DEFAULT '0' NOT NULL,
   encryption_type VARCHAR(20) DEFAULT '' NOT NULL,
   last_login TIMESTAMP DEFAULT '1979-11-03 22:05:58' NOT NULL,
   PRIMARY KEY (user_idnr)
);
CREATE UNIQUE INDEX dbmail_users_name_idx ON dbmail_users(userid);

CREATE TABLE dbmail_usermap (
  login VARCHAR(100) NOT NULL,
  sock_allow varchar(100) NOT NULL,
  sock_deny varchar(100) NOT NULL,
  userid varchar(100) NOT NULL
);
CREATE UNIQUE INDEX usermap_idx_1 ON dbmail_usermap(login, sock_allow, userid);

CREATE SEQUENCE dbmail_mailbox_idnr_seq;
CREATE TABLE dbmail_mailboxes (
   mailbox_idnr INT8 DEFAULT nextval('dbmail_mailbox_idnr_seq'),
   owner_idnr INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
   name VARCHAR(100) NOT NULL,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   no_inferiors INT2 DEFAULT '0' NOT NULL,
   no_select INT2 DEFAULT '0' NOT NULL,
   permission INT2 DEFAULT '2' NOT NULL,
   PRIMARY KEY (mailbox_idnr)
);
CREATE INDEX dbmail_mailboxes_owner_idx ON dbmail_mailboxes(owner_idnr);
CREATE INDEX dbmail_mailboxes_name_idx ON dbmail_mailboxes(name);
CREATE UNIQUE INDEX dbmail_mailboxes_owner_name_idx 
	ON dbmail_mailboxes(owner_idnr, name);

CREATE TABLE dbmail_subscription (
   user_id INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
   mailbox_id INT8 REFERENCES dbmail_mailboxes(mailbox_idnr)
	ON DELETE CASCADE ON UPDATE CASCADE,
   PRIMARY KEY (user_id, mailbox_id)
);

CREATE TABLE dbmail_acl (
    user_id INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
    mailbox_id INT8 REFERENCES dbmail_mailboxes(mailbox_idnr)
	ON DELETE CASCADE ON UPDATE CASCADE,
    lookup_flag INT2 DEFAULT '0' NOT NULL,
    read_flag INT2 DEFAULT '0' NOT NULL,
    seen_flag INT2 DEFAULT '0' NOT NULL,
    write_flag INT2 DEFAULT '0' NOT NULL,
    insert_flag INT2 DEFAULT '0' NOT NULL,
    post_flag INT2 DEFAULT '0' NOT NULL,
    create_flag INT2 DEFAULT '0' NOT NULL,
    delete_flag INT2 DEFAULT '0' NOT NULL,
    administer_flag INT2 DEFAULT '0' NOT NULL,
    PRIMARY KEY (user_id, mailbox_id)
);

CREATE SEQUENCE dbmail_physmessage_id_seq;
CREATE TABLE dbmail_physmessage (
   id INT8 DEFAULT nextval('dbmail_physmessage_id_seq'),
   messagesize INT8 DEFAULT '0' NOT NULL,   
   rfcsize INT8 DEFAULT '0' NOT NULL,
   internal_date TIMESTAMP WITHOUT TIME ZONE,
   PRIMARY KEY(id)
);

CREATE SEQUENCE dbmail_message_idnr_seq;
CREATE TABLE dbmail_messages (
   message_idnr INT8 DEFAULT nextval('dbmail_message_idnr_seq'),
   mailbox_idnr INT8 REFERENCES dbmail_mailboxes(mailbox_idnr)
	ON DELETE CASCADE ON UPDATE CASCADE,
   physmessage_id INT8 REFERENCES dbmail_physmessage(id)
	ON DELETE CASCADE ON UPDATE CASCADE,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   unique_id varchar(70) NOT NULL,
   status INT2 DEFAULT '0' NOT NULL,
   PRIMARY KEY (message_idnr)
);
CREATE INDEX dbmail_messages_1 ON dbmail_messages(mailbox_idnr);
CREATE INDEX dbmail_messages_2 ON dbmail_messages(physmessage_id);
CREATE INDEX dbmail_messages_3 ON dbmail_messages(seen_flag);
CREATE INDEX dbmail_messages_4 ON dbmail_messages(unique_id);
CREATE INDEX dbmail_messages_5 ON dbmail_messages(status);
CREATE INDEX dbmail_messages_6 ON dbmail_messages(status) WHERE status < '2';
CREATE INDEX dbmail_messages_7 ON dbmail_messages(mailbox_idnr,status,seen_flag);
CREATE INDEX dbmail_messages_8 ON dbmail_messages(mailbox_idnr,status,recent_flag);

CREATE SEQUENCE dbmail_messageblk_idnr_seq;
CREATE TABLE dbmail_messageblks (
   messageblk_idnr INT8 DEFAULT nextval('dbmail_messageblk_idnr_seq'),
   physmessage_id INT8 REFERENCES dbmail_physmessage(id)
	ON DELETE CASCADE ON UPDATE CASCADE,
   messageblk BYTEA NOT NULL,
   blocksize INT8 DEFAULT '0' NOT NULL,
   is_header INT2 DEFAULT '0' NOT NULL,
   PRIMARY KEY (messageblk_idnr)
);
CREATE INDEX dbmail_messageblks_physmessage_idx 
	ON dbmail_messageblks(physmessage_id);
CREATE INDEX dbmail_messageblks_physmessage_is_header_idx 
	ON dbmail_messageblks(physmessage_id, is_header);

CREATE TABLE dbmail_auto_notifications (
   user_idnr INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
   notify_address VARCHAR(100),
   PRIMARY KEY (user_idnr)
);

CREATE TABLE dbmail_auto_replies (
   user_idnr INT8 REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
   reply_body TEXT,
   PRIMARY KEY (user_idnr)
);

CREATE SEQUENCE dbmail_seq_pbsp_id;
CREATE TABLE dbmail_pbsp (
  idnr INT8 NOT NULL DEFAULT NEXTVAL('dbmail_seq_pbsp_id'),
  since TIMESTAMP NOT NULL DEFAULT '1970-01-01 00:00:00',
  ipnumber INET NOT NULL DEFAULT '0.0.0.0',
  PRIMARY KEY (idnr)
);
CREATE UNIQUE INDEX dbmail_idx_ipnumber ON dbmail_pbsp (ipnumber);
CREATE INDEX dbmail_idx_since ON dbmail_pbsp (since);

--- Create the user for the delivery chain:
INSERT INTO dbmail_users (userid, passwd, encryption_type) 
	VALUES ('__@!internal_delivery_user!@__', '', 'md5');
--- insert the 'anyone' user which is used for ACLs.
INSERT INTO dbmail_users (userid, passwd, encryption_type) 
	VALUES ('anyone', '', 'md5');

 
COMMIT;

