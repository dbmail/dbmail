/* $Id$
    Todo:
    - add foreign key constrains (will prevent inconsistence
    - add / remove indexes depending on performance 
*/

CREATE SEQUENCE alias_idnr_seq;
CREATE TABLE aliases (
    alias_idnr INT8 DEFAULT nextval('alias_idnr_seq'),
    alias VARCHAR(100) NOT NULL, 
    deliver_to VARCHAR(250) NOT NULL,
    client_idnr INT8 DEFAULT '0' NOT NULL,
    PRIMARY KEY (alias_idnr)
);
CREATE INDEX aliases_alias_idx ON aliases(alias);

CREATE SEQUENCE user_idnr_seq;
CREATE TABLE users (
   user_idnr INT8 DEFAULT nextval('user_idnr_seq'),
   userid VARCHAR(100) NOT NULL,
   passwd VARCHAR(32) NOT NULL,
   client_idnr INT8 DEFAULT '0' NOT NULL,
   maxmail_size INT8 DEFAULT '0' NOT NULL,
   encryption_type VARCHAR(20) DEFAULT '' NOT NULL,
   PRIMARY KEY (user_idnr)
);
CREATE UNIQUE INDEX users_id_idx ON users (userid);
CREATE INDEX users_name_idx ON users(userid);


CREATE SEQUENCE mailbox_idnr_seq;
CREATE TABLE mailboxes (
   mailbox_idnr INT8 DEFAULT nextval('mailbox_idnr_seq'),
   owner_idnr INTEGER NOT NULL,
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
   PRIMARY KEY (mailbox_idnr)
);
CREATE UNIQUE INDEX mailboxes_id_idx ON mailboxes(mailbox_idnr);
CREATE INDEX mailboxes_owner_idx ON mailboxes(owner_idnr);


CREATE SEQUENCE message_idnr_seq;
CREATE TABLE messages (
   message_idnr INT8 DEFAULT nextval('message_idnr_seq'),
   mailbox_idnr INT8 DEFAULT '0' NOT NULL,
   messagesize INT8 DEFAULT '0' NOT NULL,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   unique_id varchar(70) NOT NULL,
   internal_date DATETIME,
   status INT2 DEFAULT '000' NOT NULL,
   rfcsize INT8 DEFAULT '0' NOT NULL,
   PRIMARY KEY (message_idnr)
);
CREATE UNIQUE INDEX messages_id_idx ON messages(message_idnr);
CREATE INDEX messages_mailbox_idx ON messages(mailbox_idnr);


CREATE SEQUENCE messageblk_idnr_seq;
CREATE TABLE messageblks (
   messageblk_idnr INT8 DEFAULT nextval('messageblk_idnr_seq'),
   message_idnr INT8 DEFAULT '0' NOT NULL,
   messageblk TEXT NOT NULL,
   blocksize INT8 DEFAULT '0' NOT NULL,
   PRIMARY KEY (messageblk_idnr)
);
CREATE UNIQUE INDEX messageblks_id_idx ON messageblks(messageblk_idnr);
CREATE INDEX messageblks_msg_idx ON messageblks(message_idnr);


CREATE TABLE config (
	config_idnr INTEGER DEFAULT '0' NOT NULL,
	item VARCHAR(255) NOT NULL,
	value VARCHAR(255) NOT NULL
);
