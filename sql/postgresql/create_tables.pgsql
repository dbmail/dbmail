/*
 Copyright (C) 1999-2004, IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2014, NFG Net Facilities Group BV, support@nfg.nl

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
/* 
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

CREATE SEQUENCE dbmail_authlog_id_seq;
CREATE TABLE dbmail_authlog (
  id INT8 DEFAULT nextval('dbmail_authlog_id_seq'),
  userid VARCHAR(100),
  service VARCHAR(32),
  login_time TIMESTAMP WITHOUT TIME ZONE,
  logout_time TIMESTAMP WITHOUT TIME ZONE,
  src_ip VARCHAR(16),
  src_port INT8,
  dst_ip VARCHAR(16),
  dst_port INT8,
  status VARCHAR(32) DEFAULT 'active',
  bytes_rx INT8 DEFAULT '0' NOT NULL,
  bytes_tx INT8 DEFAULT '0' NOT NULL
);



CREATE SEQUENCE dbmail_user_idnr_seq;
CREATE TABLE dbmail_users (
   user_idnr INT8 DEFAULT nextval('dbmail_user_idnr_seq'),
   userid VARCHAR(100) NOT NULL,
   passwd VARCHAR(130) NOT NULL,
   client_idnr INT8 DEFAULT '0' NOT NULL,
   maxmail_size INT8 DEFAULT '0' NOT NULL,
   curmail_size INT8 DEFAULT '0' NOT NULL,
   maxsieve_size INT8 DEFAULT '0' NOT NULL,
   cursieve_size INT8 DEFAULT '0' NOT NULL,
   encryption_type VARCHAR(20) DEFAULT '' NOT NULL,
   last_login TIMESTAMP DEFAULT '1979-11-03 22:05:58' NOT NULL,
   PRIMARY KEY (user_idnr)
);

CREATE UNIQUE INDEX dbmail_users_name_idx ON dbmail_users(userid);
CREATE INDEX dbmail_users_2 ON dbmail_users (lower(userid));

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
   name VARCHAR(255) NOT NULL,
   seen_flag INT2 DEFAULT '0' NOT NULL,
   answered_flag INT2 DEFAULT '0' NOT NULL,
   deleted_flag INT2 DEFAULT '0' NOT NULL,
   flagged_flag INT2 DEFAULT '0' NOT NULL,
   recent_flag INT2 DEFAULT '0' NOT NULL,
   draft_flag INT2 DEFAULT '0' NOT NULL,
   no_inferiors INT2 DEFAULT '0' NOT NULL,
   no_select INT2 DEFAULT '0' NOT NULL,
   permission INT2 DEFAULT '2' NOT NULL,
   seq INT8 DEFAULT '0' NOT NULL,
   PRIMARY KEY (mailbox_idnr)
);
CREATE INDEX dbmail_mailboxes_owner_idx ON dbmail_mailboxes(owner_idnr);
CREATE INDEX dbmail_mailboxes_name_idx ON dbmail_mailboxes(name);
CREATE INDEX dbmail_mailboxes_seq ON dbmail_mailboxes(seq);
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
    deleted_flag INT2 DEFAULT '0' NOT NULL,
    expunge_flag INT2 DEFAULT '0' NOT NULL,
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
   start_date timestamp without time zone not null,
   stop_date timestamp without time zone not null,
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
--- Create the 'anyone' user which is used for ACLs.
INSERT INTO dbmail_users (userid, passwd, encryption_type) 
	VALUES ('anyone', '', 'md5');
--- Create the user to own #Public mailboxes
INSERT INTO dbmail_users (userid, passwd, encryption_type) 
	VALUES ('__public__', '', 'md5');

 
CREATE SEQUENCE dbmail_headervalue_id_seq;
CREATE TABLE dbmail_headervalue (
        id INT8 NOT NULL DEFAULT nextval('dbmail_headervalue_id_seq'),
	hash VARCHAR(256) NOT NULL,
        headervalue   TEXT NOT NULL DEFAULT '',
        sortfield     VARCHAR(255) DEFAULT NULL,
        datefield     TIMESTAMP WITHOUT TIME ZONE,
        PRIMARY KEY (id)
);

CREATE INDEX dbmail_headervalue_1 ON dbmail_headervalue USING btree (hash);
CREATE INDEX dbmail_headervalue_2 ON dbmail_headervalue USING btree (sortfield);
CREATE INDEX dbmail_headervalue_3 ON dbmail_headervalue USING btree (datefield);

CREATE SEQUENCE dbmail_headername_id_seq;
CREATE TABLE dbmail_headername (
        id  INT8 NOT NULL DEFAULT nextval('dbmail_headername_id_seq'),
        headername    VARCHAR(100) NOT NULL DEFAULT 'BROKEN_HEADER',
        PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername(lower(headername));

CREATE TABLE dbmail_header (
        physmessage_id      INT8 NOT NULL
		REFERENCES dbmail_physmessage(id)
                ON UPDATE CASCADE ON DELETE CASCADE,
        headername_id  INT8 NOT NULL
                REFERENCES dbmail_headername(id)
                ON UPDATE CASCADE ON DELETE CASCADE,
        headervalue_id      INT8 NOT NULL
                REFERENCES dbmail_headervalue(id)
                ON UPDATE CASCADE ON DELETE CASCADE,
        PRIMARY KEY (physmessage_id,headername_id,headervalue_id)
);
 
CREATE INDEX dbmail_header_headername_id_key on dbmail_header(headername_id);   
CREATE INDEX dbmail_header_headervalue_id_key on dbmail_header(headervalue_id); 
CREATE INDEX dbmail_header_physmessage_id_key on dbmail_header(physmessage_id); 


CREATE SEQUENCE dbmail_referencesfield_idnr_seq;
CREATE TABLE dbmail_referencesfield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id) 
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_referencesfield_idnr_seq'),
	referencesfield	VARCHAR(255) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_referencesfield_1 ON dbmail_referencesfield(physmessage_id, referencesfield);


CREATE TABLE dbmail_replycache (
    to_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    from_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    handle    character varying(100) DEFAULT ''::character varying,
    lastseen timestamp without time zone NOT NULL
);
CREATE UNIQUE INDEX replycache_1 ON dbmail_replycache USING btree (to_addr, from_addr, handle);

CREATE SEQUENCE dbmail_sievescripts_idnr_seq;
CREATE TABLE dbmail_sievescripts (
	id		INT8 DEFAULT nextval('dbmail_sievescripts_idnr_seq'),
        owner_idnr	INT8 NOT NULL
			REFERENCES dbmail_users(user_idnr)
			ON UPDATE CASCADE ON DELETE CASCADE,
	active		INT2 DEFAULT '0' NOT NULL,
	name		VARCHAR(100) NOT NULL DEFAULT '',
	script		TEXT NOT NULL DEFAULT '',
	PRIMARY KEY	(id)
);

CREATE UNIQUE INDEX dbmail_sievescripts_1 on dbmail_sievescripts(owner_idnr,name);
CREATE INDEX dbmail_sievescripts_2 on dbmail_sievescripts(owner_idnr,active);

CREATE SEQUENCE dbmail_envelope_idnr_seq;
CREATE TABLE dbmail_envelope (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_envelope_idnr_seq'),
	envelope	TEXT NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_envelope_1 ON dbmail_envelope(physmessage_id);
CREATE UNIQUE INDEX dbmail_envelope_2 ON dbmail_envelope(physmessage_id, id);

CREATE SEQUENCE dbmail_mimeparts_id_seq;
CREATE TABLE dbmail_mimeparts (
    id bigint NOT NULL DEFAULT nextval('dbmail_mimeparts_id_seq'),
    hash character(256) NOT NULL,
    data bytea NOT NULL,
    size bigint NOT NULL,
    PRIMARY KEY (id)
);

CREATE INDEX dbmail_mimeparts_1 ON dbmail_mimeparts USING btree (hash);

CREATE TABLE dbmail_partlists (
    physmessage_id bigint NOT NULL,
    is_header smallint DEFAULT (0)::smallint NOT NULL,
    part_key integer DEFAULT 0 NOT NULL,
    part_depth integer DEFAULT 0 NOT NULL,
    part_order integer DEFAULT 0 NOT NULL,
    part_id bigint NOT NULL
);

CREATE INDEX dbmail_partlists_1 ON dbmail_partlists USING btree (physmessage_id);
CREATE INDEX dbmail_partlists_2 ON dbmail_partlists USING btree (part_id);
CREATE UNIQUE INDEX message_parts ON dbmail_partlists(physmessage_id, part_key, part_depth, part_order);

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_part_id_fkey FOREIGN KEY (part_id) REFERENCES dbmail_mimeparts(id) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_physmessage_id_fkey FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage(id) ON UPDATE CASCADE ON DELETE CASCADE;

CREATE TABLE dbmail_keywords (
	message_idnr bigint NOT NULL,
	keyword varchar(64) NOT NULL
);
ALTER TABLE ONLY dbmail_keywords
    ADD CONSTRAINT dbmail_keywords_pkey PRIMARY KEY (message_idnr, keyword);
ALTER TABLE ONLY dbmail_keywords
    ADD CONSTRAINT dbmail_keywords_fkey FOREIGN KEY (message_idnr) REFERENCES dbmail_messages (message_idnr) ON DELETE CASCADE ON UPDATE CASCADE;

CREATE SEQUENCE dbmail_filters_id_seq;
CREATE TABLE dbmail_filters (
	user_id      INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
	id           INT8 NOT NULL DEFAULT nextval('dbmail_filters_id_seq'),
	headername   varchar(128) NOT NULL,
	headervalue  varchar(255) NOT NULL,	
	mailbox      varchar(100) NOT NULL,	
	PRIMARY KEY (user_id, id)
);

CREATE VIEW dbmail_fromfield AS
        SELECT physmessage_id,sortfield AS fromfield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='from';

CREATE VIEW dbmail_ccfield AS
        SELECT physmessage_id,sortfield AS ccfield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='cc';

CREATE VIEW dbmail_tofield AS
        SELECT physmessage_id,sortfield AS tofield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='to';

CREATE VIEW dbmail_subjectfield AS
        SELECT physmessage_id,headervalue AS subjectfield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='subject';

CREATE VIEW dbmail_datefield AS
        SELECT physmessage_id,datefield,sortfield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='date';


COMMIT;
