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

-- required tables:
CREATE TABLE aliases_1 AS SELECT * FROM aliases;
CREATE TABLE users_1 AS SELECT * FROM users;
CREATE TABLE mailboxes_1 AS SELECT * FROM mailboxes;
CREATE TABLE messages_1 AS SELECT * FROM messages;
CREATE TABLE messageblks_1 AS SELECT * FROM messageblks;

DROP TABLE aliases;
DROP TABLE messageblks;
DROP TABLE messages;
DROP TABLE mailboxes;
DROP TABLE users;
DROP TABLE pbsp;
-- create dbmail-2 tables

CREATE TABLE aliases (
    alias_idnr INT8 DEFAULT nextval('alias_idnr_seq'),
    alias VARCHAR(100) NOT NULL, 
    deliver_to VARCHAR(250) NOT NULL,
    client_idnr INT8 DEFAULT '0' NOT NULL,
    PRIMARY KEY (alias_idnr)
);

CREATE INDEX aliases_alias_idx ON aliases(alias);
CREATE INDEX aliases_alias_low_idx ON aliases(lower(alias));

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
CREATE UNIQUE INDEX users_name_idx ON users(userid);

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
   PRIMARY KEY (mailbox_idnr)
);
CREATE INDEX mailboxes_owner_idx ON mailboxes(owner_idnr);
CREATE INDEX mailboxes_name_idx ON mailboxes(name);
CREATE INDEX mailboxes_owner_name_idx ON mailboxes(owner_idnr, name);

CREATE TABLE subscription (
	user_id INT8 NOT NULL,
	mailbox_id INT8 NOT NULL,
	PRIMARY KEY (user_id, mailbox_id)
);

CREATE TABLE acl (
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
    PRIMARY KEY (user_id, mailbox_id)
);

CREATE SEQUENCE physmessage_id_seq;
CREATE TABLE physmessage (
   id INT8 DEFAULT nextval('physmessage_id_seq'),
   messagesize INT8 DEFAULT '0' NOT NULL,   
   rfcsize INT8 DEFAULT '0' NOT NULL,
   internal_date TIMESTAMP WITHOUT TIME ZONE,
   PRIMARY KEY(id)
);

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
   PRIMARY KEY (message_idnr)
);

CREATE INDEX messages_mailbox_idx ON messages(mailbox_idnr);
CREATE INDEX messages_physmessage_idx ON messages(physmessage_id);
CREATE INDEX messages_seen_flag_idx ON messages(seen_flag);
CREATE INDEX messages_unique_id_idx ON messages(unique_id);
CREATE INDEX messages_status_idx ON messages(status);

CREATE TABLE messageblks (
   messageblk_idnr INT8 DEFAULT nextval('messageblk_idnr_seq'),
   physmessage_id INT8 DEFAULT '0' NOT NULL,
   messageblk TEXT NOT NULL,
   blocksize INT8 DEFAULT '0' NOT NULL,
   PRIMARY KEY (messageblk_idnr)
);
CREATE INDEX messageblks_physmessage_idx ON messageblks(physmessage_id);

CREATE SEQUENCE seq_pbsp_id;
CREATE TABLE pbsp (
  idnr BIGINT NOT NULL DEFAULT NEXTVAL('seq_pbsp_id'),
  since TIMESTAMP NOT NULL DEFAULT '1970-01-01 00:00:00',
  ipnumber VARCHAR(40) NOT NULL DEFAULT '',
  PRIMARY KEY (idnr)
);
CREATE UNIQUE INDEX idx_ipnumber ON pbsp (ipnumber);
CREATE INDEX idx_since ON pbsp (since);


-- fillerup

INSERT INTO aliases SELECT * from aliases_1;

INSERT INTO subscription ( user_id, mailbox_id ) SELECT owner_idnr, mailbox_idnr FROM mailboxes_1 where is_subscribed > 0;

INSERT INTO mailboxes ( mailbox_idnr, owner_idnr, name, seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, no_inferiors, no_select, permission )
SELECT mailbox_idnr, owner_idnr, name, seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, no_inferiors, no_select, permission FROM mailboxes_1;

INSERT INTO messages ( message_idnr, mailbox_idnr, seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, unique_id )
SELECT message_idnr, mailbox_idnr, seen_flag, answered_flag, deleted_flag, flagged_flag, recent_flag, draft_flag, unique_id FROM messages_1;

INSERT INTO physmessage ( id, messagesize, rfcsize, internal_date) 
SELECT message_idnr, messagesize, rfcsize, internal_date FROM messages_1; 

UPDATE messages SET physmessage_id = message_idnr;

INSERT INTO messageblks ( messageblk_idnr, physmessage_id, messageblk, blocksize )
SELECT messageblk_idnr, message_idnr, messageblk, blocksize FROM messageblks_1;


INSERT INTO users ( user_idnr, userid, passwd, client_idnr, maxmail_size, encryption_type, last_login, curmail_size )
SELECT u.*, sum(p.messagesize) AS curmail_size 
FROM users_1 u 
LEFT JOIN mailboxes b ON b.owner_idnr = u.user_idnr 
LEFT JOIN messages m ON m.mailbox_idnr = b.mailbox_idnr 
LEFT JOIN physmessage p ON m.physmessage_id = p.id 
GROUP BY u.user_idnr, u.userid, u.passwd, u.client_idnr, u.maxmail_size, u.encryption_type,u.last_login;

--- drop the old tables
DROP TABLE aliases_1, users_1, mailboxes_1, messages_1, messageblks_1;

ALTER TABLE mailboxes ADD   FOREIGN KEY (owner_idnr) REFERENCES users(user_idnr) ON DELETE CASCADE;
ALTER TABLE subscription ADD	FOREIGN KEY (user_id) REFERENCES users(user_idnr) ON DELETE CASCADE;
ALTER TABLE subscription ADD	FOREIGN KEY (mailbox_id) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE;
ALTER TABLE acl ADD FOREIGN KEY (user_id) REFERENCES users(user_idnr) ON DELETE CASCADE;
ALTER TABLE acl ADD FOREIGN KEY (mailbox_id) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE;
ALTER TABLE messages ADD FOREIGN KEY (physmessage_id) REFERENCES physmessage(id) ON DELETE CASCADE;
ALTER TABLE messages ADD FOREIGN KEY (mailbox_idnr) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE;
ALTER TABLE messageblks ADD FOREIGN KEY (physmessage_id) REFERENCES physmessage (id) ON DELETE CASCADE;

--- Create the user for the delivery chain:
INSERT INTO users (userid, passwd, encryption_type)
	VALUES ('__@!internal_delivery_user!@__', '', 'md5');

