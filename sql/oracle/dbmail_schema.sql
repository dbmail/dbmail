-- dbmail release 2.3.7 Oracle schema
--
-- Copyright (c) 2004-2007, NFG Net Facilities Group BV, support@nfg.nl
-- Copyright (c) 2006 Aaron Stone, aaron@serendipity.cx
-- 
-- This program is free software; you can redistribute it and/or 
-- modify it under the terms of the GNU General Public License 
-- as published by the Free Software Foundation; either 
-- version 2 of the License, or (at your option) any later 
-- version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program; if not, write to the Free Software
-- Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
-- 


alter session set nls_date_format='YYYY-MM-DD';
alter session set nls_timestamp_format='YYYY-MM-DD HH24:MI:SS';
alter session set nls_timestamp_tz_format='YYYY-MM-DD HH24:MI:SS';
alter session set time_zone=dbtimezone;

--
-- Table structure for table `dbmail_acl`
--

CREATE SEQUENCE sq_dbmail_authlog;
CREATE TABLE dbmail_authlog (
  id number(20) NOT NULL,
  userid varchar2(100) default NULL,
  service varchar2(32) default NULL,
  login_time date default NULL,
  logout_time date default NULL,
  src_ip varchar2(16) default NULL,
  src_port number(11) default NULL,
  dst_ip varchar2(16) default NULL,
  dst_port number(11) default NULL,
  status varchar2(32) default 'active',
  bytes_rx number(20) default '0' NOT NULL,
  bytes_tx number(20) default '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_authlog_idx ON dbmail_authlog (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_authlog ADD CONSTRAINT dbmail_authlog_pk PRIMARY KEY (id) USING INDEX dbmail_authlog_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_authlog
BEFORE INSERT ON dbmail_authlog FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_authlog.nextval
 INTO :new.id
 FROM dual;
END;
/


--
-- Table structure for table `dbmail_acl`
--

CREATE TABLE dbmail_acl (
  user_id number(20) DEFAULT '0' NOT NULL,
  mailbox_id number(20) DEFAULT '0' NOT NULL,
  lookup_flag number(1) DEFAULT '0' NOT NULL,
  read_flag number(1) DEFAULT '0' NOT NULL,
  seen_flag number(1) DEFAULT '0' NOT NULL,
  write_flag number(1) DEFAULT '0' NOT NULL,
  insert_flag number(1) DEFAULT '0' NOT NULL,
  post_flag number(1) DEFAULT '0' NOT NULL,
  create_flag number(1) DEFAULT '0' NOT NULL,
  delete_flag number(1) DEFAULT '0' NOT NULL,
  deleted_flag number(1) DEFAULT '0' NOT NULL,
  expunge_flag number(1) DEFAULT '0' NOT NULL,
  administer_flag number(1) DEFAULT '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_acl_idx ON dbmail_acl (user_id, mailbox_id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_acl ADD CONSTRAINT dbmail_acl_pk PRIMARY KEY (user_id, mailbox_id) USING INDEX dbmail_acl_idx;
CREATE  INDEX dbmail_acl_mailbox_id_idx ON dbmail_acl (mailbox_id) TABLESPACE DBMAIL_TS_IDX;



--
-- Table structure for table `dbmail_aliases`
--
CREATE SEQUENCE sq_dbmail_aliases;
CREATE TABLE dbmail_aliases (
  alias_idnr number(20) NOT NULL,
  alias varchar2(255) default  NULL,
  deliver_to varchar2(255) default NULL,
  client_idnr number(20) default '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_aliases_idx ON dbmail_aliases (alias_idnr) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_aliases ADD CONSTRAINT dbmail_aliases_pk PRIMARY KEY (alias_idnr) USING INDEX dbmail_aliases_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_aliases
BEFORE INSERT ON dbmail_aliases FOR EACH ROW
WHEN (
new.alias_idnr IS NULL OR new.alias_idnr = 0
      )
BEGIN
 SELECT sq_dbmail_aliases.nextval
 INTO :new.alias_idnr
 FROM dual;
END;
/

CREATE  INDEX dbmail_aliases_client_idnr_idx ON dbmail_aliases (client_idnr) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_envelope`
--
CREATE SEQUENCE sq_dbmail_envelope;
CREATE TABLE dbmail_envelope (
  id number(20) NOT NULL,
  physmessage_id number(20) DEFAULT '0' NOT NULL,
  envelope clob NOT NULL
);
CREATE UNIQUE INDEX dbmail_envelope_idx ON dbmail_envelope (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_envelope ADD CONSTRAINT dbmail_envelope_pk PRIMARY KEY (id) USING INDEX dbmail_envelope_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_envelope
BEFORE INSERT ON dbmail_envelope FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_envelope.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_envelope_idx1 ON dbmail_envelope (physmessage_id) TABLESPACE DBMAIL_TS_IDX;
CREATE UNIQUE INDEX dbmail_envelope_idx2 ON dbmail_envelope (physmessage_id, id) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_filters`
--
CREATE SEQUENCE sq_dbmail_filters;
CREATE TABLE dbmail_filters (
  id number(20) NOT NULL,
  user_id number(20) NOT NULL,
  headername varchar2(255) NOT NULL,
  headervalue varchar2(255) NOT NULL,
  mailbox varchar2(255) NOT NULL
);
CREATE UNIQUE INDEX dbmail_filters_idx ON dbmail_filters (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_filters ADD CONSTRAINT dbmail_filters_pk PRIMARY KEY (id) USING INDEX dbmail_filters_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_filters
BEFORE INSERT ON dbmail_filters FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_filters.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE INDEX dbmail_filters_idx1 ON dbmail_filters (user_id) TABLESPACE DBMAIL_TS_IDX;
--
-- Table structure for table `dbmail_header`
--

CREATE TABLE dbmail_header (
  physmessage_id number(20) NOT NULL,
  headername_id number(20) NOT NULL,
  headervalue_id number(20) NOT NULL
);
CREATE UNIQUE INDEX dbmail_header_idx ON dbmail_header (physmessage_id, headername_id, headervalue_id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_header ADD CONSTRAINT dbmail_header_pk PRIMARY KEY (physmessage_id, headername_id, headervalue_id) USING INDEX dbmail_header_idx;
CREATE INDEX dbmail_header_idx3 ON dbmail_header (headervalue_id) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_header_idx5 ON dbmail_header (physmessage_id, headervalue_id) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_header_idx6 ON dbmail_header (headername_id, headervalue_id) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_headername`
--
CREATE SEQUENCE sq_dbmail_headername;
CREATE TABLE dbmail_headername (
  id number(20) NOT NULL,
  headername varchar2(255) default NULL
);
CREATE UNIQUE INDEX dbmail_headername_idx ON dbmail_headername (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_headername ADD CONSTRAINT dbmail_headername_pk PRIMARY KEY (id) USING INDEX dbmail_headername_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_headername
BEFORE INSERT ON dbmail_headername FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_headername.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_headername_idx1 ON dbmail_headername (NLSSORT(headername, 'NLS_SORT=BINARY_CI')) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_headervalue`
--
CREATE SEQUENCE sq_dbmail_headervalue;
CREATE TABLE dbmail_headervalue (
  id number(20) NOT NULL,
  hash varchar2(255) NOT NULL,
  headervalue clob NOT NULL,
  sortfield varchar2(255) default NULL,
  datefield timestamp default NULL
);
CREATE UNIQUE INDEX dbmail_headervalue_idx ON dbmail_headervalue (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_headervalue ADD CONSTRAINT dbmail_headervalue_pk PRIMARY KEY (id) USING INDEX dbmail_headervalue_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_headervalue
BEFORE INSERT ON dbmail_headervalue FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_headervalue.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE INDEX dbmail_headervalue_idx1 ON dbmail_headervalue (hash) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_headervalue_idx3 ON dbmail_headervalue (sortfield) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_headervalue_idx4 ON dbmail_headervalue (datefield) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_keywords`
--

CREATE TABLE dbmail_keywords (
  message_idnr number(20) default '0' NOT NULL,
  keyword varchar2(255) NOT NULL
);
CREATE UNIQUE INDEX dbmail_keywords_idx ON dbmail_keywords (message_idnr, keyword) TABLESPACE DBMAIL_TS_IDX;
CREATE UNIQUE INDEX dbmail_keywords_idx_ci ON dbmail_keywords (message_idnr, NLSSORT(keyword, 'NLS_SORT=BINARY_CI')) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_keywords ADD CONSTRAINT dbmail_keywords_pk PRIMARY KEY (message_idnr, keyword) USING INDEX dbmail_keywords_idx;

--
-- Table structure for table `dbmail_mailboxes`
--

CREATE SEQUENCE sq_dbmail_mailboxes;
CREATE TABLE dbmail_mailboxes (
  mailbox_idnr number(20) NOT NULL,
  owner_idnr number(20) DEFAULT '0' NOT NULL,
  name varchar(255)  default NULL,
  seen_flag number(1) DEFAULT '0' NOT NULL,
  answered_flag number(1) DEFAULT '0' NOT NULL,
  deleted_flag number(1) DEFAULT '0' NOT NULL,
  flagged_flag number(1) DEFAULT '0' NOT NULL,
  recent_flag number(1) DEFAULT '0' NOT NULL,
  draft_flag number(1) DEFAULT '0' NOT NULL,
  no_inferiors number(1) DEFAULT '0' NOT NULL,
  no_select number(1) DEFAULT '0' NOT NULL,
  permission number(1) DEFAULT '2',
  seq number(20) DEFAULT '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_mailboxes_idx ON dbmail_mailboxes (mailbox_idnr) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_mailboxes ADD CONSTRAINT dbmail_mailboxes_pk PRIMARY KEY (mailbox_idnr) USING INDEX dbmail_mailboxes_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_mailboxes
BEFORE INSERT ON dbmail_mailboxes FOR EACH ROW
WHEN (
new.mailbox_idnr IS NULL OR new.mailbox_idnr = 0
      )
BEGIN
 SELECT sq_dbmail_mailboxes.nextval
 INTO :new.mailbox_idnr
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_mailboxes_owner_id_idx ON dbmail_mailboxes (owner_idnr, NLSSORT(name,'NLS_SORT=BINARY_CI')) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_mailboxes_name_idx ON dbmail_mailboxes (NLSSORT(name,'NLS_SORT=BINARY_CI')) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_mailboxes_seq_idx ON dbmail_mailboxes (seq) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_messages`
--
CREATE SEQUENCE sq_dbmail_messages;
CREATE TABLE dbmail_messages (
  message_idnr number(20) NOT NULL,
  mailbox_idnr number(20) DEFAULT '0' NOT NULL,
  physmessage_id number(20) DEFAULT '0' NOT NULL,
  seen_flag number(1) DEFAULT '0' NOT NULL,
  answered_flag number(1) DEFAULT '0' NOT NULL,
  deleted_flag number(1) DEFAULT '0' NOT NULL,
  flagged_flag number(1) DEFAULT '0' NOT NULL,
  recent_flag number(1) DEFAULT '0' NOT NULL,
  draft_flag number(1) DEFAULT '0' NOT NULL,
  unique_id varchar2(70) default NULL,
  status number(3) DEFAULT '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_messages_idx ON dbmail_messages (message_idnr) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_messages ADD CONSTRAINT dbmail_messages_pk PRIMARY KEY (message_idnr) USING INDEX dbmail_messages_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_messages
BEFORE INSERT ON dbmail_messages FOR EACH ROW
WHEN (
new.message_idnr IS NULL OR new.mailbox_idnr = 0
      )
BEGIN
 SELECT sq_dbmail_messages.nextval
 INTO :new.message_idnr
 FROM dual;
END;
/

CREATE INDEX dbmail_messages_phsmsg_id_idx ON dbmail_messages (physmessage_id) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_messages_mbox_idnr_idx ON dbmail_messages (mailbox_idnr) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_messages_seen_flag_idx ON dbmail_messages (seen_flag) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_messages_unique_id_idx ON dbmail_messages (unique_id ) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_messages_status_idx ON dbmail_messages (status) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_messages_mbox_stat_idx ON dbmail_messages (mailbox_idnr, status) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_mimeparts`
--

CREATE SEQUENCE sq_dbmail_mimeparts;
CREATE TABLE dbmail_mimeparts (
  id number(20) NOT NULL,
  hash varchar2(128) NOT NULL,
  data clob,
  "size" number(20) DEFAULT '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_mimeparts_idx ON dbmail_mimeparts (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_mimeparts ADD CONSTRAINT dbmail_mimeparts_pk PRIMARY KEY (id) USING INDEX dbmail_mimeparts_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_mimeparts
BEFORE INSERT ON dbmail_mimeparts FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_mimeparts.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE INDEX dbmail_mimeparts_hash_idx ON dbmail_mimeparts (hash) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_partlists`
--

CREATE TABLE dbmail_partlists (
  physmessage_id number(20) DEFAULT '0' NOT NULL,
  is_header number(1) DEFAULT '0' NOT NULL,
  part_key number(6) DEFAULT '0' NOT NULL,
  part_depth number(6) DEFAULT '0' NOT NULL,
  part_order number(6) DEFAULT '0' NOT NULL,
  part_id number(20) DEFAULT '0' NOT NULL
);
CREATE INDEX dbmail_partlists_phmsg_id_idx ON dbmail_partlists (physmessage_id) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_partlists_part_id_idx ON dbmail_partlists (part_id) TABLESPACE DBMAIL_TS_IDX;
CREATE UNIQUE INDEX dbmail_partlists_mparts_idx ON dbmail_partlists (physmessage_id,part_key,part_depth,part_order) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_pbsp`
--

CREATE SEQUENCE sq_dbmail_pbsp;
CREATE TABLE dbmail_pbsp (
  idnr number(20) NOT NULL,
  since timestamp DEFAULT TO_TIMESTAMP('0001-01-01 00:00:00','YYYY-MM-DD HH24:MI:SS') NOT NULL,
  ipnumber varchar2(40) NOT NULL
);
CREATE UNIQUE INDEX dbmail_pbsp_idx ON dbmail_pbsp (idnr) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_pbsp ADD CONSTRAINT dbmail_pbsp_pk PRIMARY KEY (idnr) USING INDEX dbmail_pbsp_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_pbsp
BEFORE INSERT ON dbmail_pbsp FOR EACH ROW
WHEN (
new.idnr IS NULL OR new.idnr = 0
      )
BEGIN
 SELECT sq_dbmail_pbsp.nextval
 INTO :new.idnr
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_pbsp_ipnumber_idx ON dbmail_pbsp (ipnumber) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_pbsp_since_idx ON dbmail_pbsp (since) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_physmessage`
--

CREATE SEQUENCE sq_dbmail_physmessage;
CREATE TABLE dbmail_physmessage (
  id number(20) NOT NULL,
  messagesize number(20) default '0' NOT NULL,
  rfcsize number(20) default '0' NOT NULL,
  internal_date timestamp default TO_TIMESTAMP('0001-01-01 00:00:00','YYYY-MM-DD HH24:MI:SS') NOT NULL
);
CREATE UNIQUE INDEX dbmail_physmessage_idx ON dbmail_physmessage (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_physmessage ADD CONSTRAINT dbmail_physmessage_pk PRIMARY KEY (id) USING INDEX dbmail_physmessage_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_physmessage
BEFORE INSERT ON dbmail_physmessage FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_physmessage.nextval
 INTO :new.id
 FROM dual;
END;
/

--
-- Table structure for table `dbmail_referencesfield`
--

CREATE SEQUENCE sq_dbmail_referencesfield;
CREATE TABLE dbmail_referencesfield (
  id number(20) NOT NULL,
  physmessage_id number(20) DEFAULT '0' NOT NULL,
  referencesfield varchar2(255) default NULL
);
CREATE UNIQUE INDEX dbmail_referencesfield_idx ON dbmail_referencesfield (id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_referencesfield ADD CONSTRAINT dbmail_referencesfield_pk PRIMARY KEY (id) USING INDEX dbmail_referencesfield_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_referencesfield
BEFORE INSERT ON dbmail_referencesfield FOR EACH ROW
WHEN (
new.id IS NULL OR new.id = 0
      )
BEGIN
 SELECT sq_dbmail_referencesfield.nextval
 INTO :new.id
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_referencesfield_msg_idx ON dbmail_referencesfield (physmessage_id,referencesfield) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_replycache`
--

CREATE TABLE dbmail_replycache (
  to_addr varchar2(255) DEFAULT NULL,
  from_addr varchar2(255) DEFAULT NULL,
  handle varchar2(255) DEFAULT NULL,
  lastseen timestamp DEFAULT TO_TIMESTAMP('0001-01-01 00:00:00','YYYY-MM-DD HH24:MI:SS') NOT NULL
);

CREATE UNIQUE INDEX dbmail_replycache_idx ON dbmail_replycache (to_addr,from_addr,handle) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_sievescripts`
--

CREATE TABLE dbmail_sievescripts (
  owner_idnr number(20) default '0' NOT NULL,
  name varchar2(255) NOT NULL,
  script clob,
  active number(1) default '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_sievescripts_idx ON dbmail_sievescripts (owner_idnr,name) TABLESPACE DBMAIL_TS_IDX;
CREATE INDEX dbmail_sievescripts_name_idx ON dbmail_sievescripts (name) TABLESPACE DBMAIL_TS_IDX;
--CREATE INDEX dbmail_sievescripts_owner_idx ON dbmail_sievescripts (owner_idnr) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_subscription`
--

CREATE TABLE dbmail_subscription (
  user_id number(20) default '0' NOT NULL,
  mailbox_id number(20) default '0' NOT NULL
);
CREATE UNIQUE INDEX dbmail_subscription_idx ON dbmail_subscription (user_id,mailbox_id) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_subscription ADD CONSTRAINT dbmail_subscription_pk PRIMARY KEY (user_id,mailbox_id) USING INDEX dbmail_subscription_idx;
CREATE INDEX dbmail_subscription_mbox_idx ON dbmail_subscription (mailbox_id) TABLESPACE DBMAIL_TS_IDX;


--
-- Table structure for table `dbmail_usermap`
--

CREATE TABLE dbmail_usermap (
  login varchar2(255) NOT NULL,
  sock_allow varchar2(255) NOT NULL,
  sock_deny varchar2(255) NOT NULL,
  userid varchar2(255) NOT NULL
);
CREATE UNIQUE INDEX dbmail_usermap_idx ON dbmail_usermap (login,sock_allow,userid) TABLESPACE DBMAIL_TS_IDX;

--
-- Table structure for table `dbmail_users`
--

CREATE SEQUENCE sq_dbmail_users;
CREATE TABLE dbmail_users (
  user_idnr number(20) NOT NULL,
  userid varchar2(255) default NULL,
  passwd varchar2(255) default NULL,
  client_idnr number(20) default '0' NOT NULL,
  maxmail_size number(20) default '0' NOT NULL,
  curmail_size number(20) default '0' NOT NULL,
  maxsieve_size number(20) default '0' NOT NULL,
  cursieve_size number(20) default '0' NOT NULL,
  encryption_type varchar2(255)  default NULL,
  last_login timestamp default TO_TIMESTAMP('1979-11-03 22:05:58','YYYY-MM-DD HH24:MI:SS') NOT NULL
  spasswd varchar2(255) default NULL,
  saction number(1) default '0' NOT NULL,
  active number(1) default '1' NOT NULL
);
CREATE UNIQUE INDEX dbmail_users_idx ON dbmail_users (user_idnr) TABLESPACE DBMAIL_TS_IDX;
ALTER TABLE dbmail_users ADD CONSTRAINT dbmail_users_pk PRIMARY KEY (user_idnr) USING INDEX dbmail_users_idx;
CREATE OR REPLACE TRIGGER ai_dbmail_users
BEFORE INSERT ON dbmail_users FOR EACH ROW
WHEN (
new.user_idnr IS NULL OR new.user_idnr = 0
      )
BEGIN
 SELECT sq_dbmail_users.nextval
 INTO :new.user_idnr
 FROM dual;
END;
/

CREATE UNIQUE INDEX dbmail_users_userid_idx ON dbmail_users (NLSSORT(userid,'NLS_SORT=BINARY_CI')) TABLESPACE DBMAIL_TS_IDX;

-- FK
ALTER TABLE dbmail_acl ADD CONSTRAINT dbmail_acl_fk1 FOREIGN KEY (user_id) REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE;
ALTER TABLE dbmail_acl ADD CONSTRAINT dbmail_acl_fk2 FOREIGN KEY (mailbox_id) REFERENCES dbmail_mailboxes(mailbox_idnr) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_envelope ADD CONSTRAINT dbmail_envelope_fk1 FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_filters ADD CONSTRAINT dbmail_filters_fk1 FOREIGN KEY (user_id) REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_header ADD CONSTRAINT dbmail_header_fk1 FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;
ALTER TABLE dbmail_header ADD CONSTRAINT dbmail_header_fk2 FOREIGN KEY (headername_id) REFERENCES dbmail_headername (id) ON DELETE CASCADE;
ALTER TABLE dbmail_header ADD CONSTRAINT dbmail_header_fk3 FOREIGN KEY (headervalue_id) REFERENCES dbmail_headervalue (id) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_keywords ADD CONSTRAINT dbmail_keywords_fk1 FOREIGN KEY (message_idnr) REFERENCES dbmail_messages (message_idnr) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_mailboxes ADD CONSTRAINT dbmail_mailboxes_fk1 FOREIGN KEY (owner_idnr) REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_messages ADD CONSTRAINT dbmail_messages_fk1 FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;
ALTER TABLE dbmail_messages ADD CONSTRAINT dbmail_messages_fk2 FOREIGN KEY (mailbox_idnr) REFERENCES dbmail_mailboxes (mailbox_idnr) ON DELETE CASCADE;
ALTER TABLE dbmail_partlists ADD CONSTRAINT dbmail_partlists_fk1 FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;
ALTER TABLE dbmail_partlists ADD CONSTRAINT dbmail_partlists_fk2 FOREIGN KEY (part_id) REFERENCES dbmail_mimeparts (id) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_referencesfield ADD CONSTRAINT dbmail_referencesfield_fk1 FOREIGN KEY (physmessage_id) 
	REFERENCES dbmail_physmessage (id) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_sievescripts ADD CONSTRAINT dbmail_sievescripts_fk1 FOREIGN KEY (owner_idnr) 
	REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE;
-- FK
ALTER TABLE dbmail_subscription ADD CONSTRAINT dbmail_subscription_fk1 FOREIGN KEY (user_id) 
	REFERENCES dbmail_users (user_idnr) ON DELETE CASCADE;
ALTER TABLE dbmail_subscription ADD CONSTRAINT dbmail_subscription_fk2 FOREIGN KEY (mailbox_id) 
	REFERENCES dbmail_mailboxes (mailbox_idnr) ON DELETE CASCADE;

--
-- views for IMAP sort
--

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



-- Create the required built-in users for the delivery chain, anyone acls, and #public mailboxes
INSERT INTO dbmail_users (userid, passwd, encryption_type) VALUES 
	('__@!internal_delivery_user!@__', '', 'md5');
INSERT INTO dbmail_users (userid, passwd, encryption_type) VALUES 
	('anyone', '', 'md5');
INSERT INTO dbmail_users (userid, passwd, encryption_type) VALUES 
	('__public__', '', 'md5');

CREATE OR REPLACE PACKAGE DBMAIL_UTILS
IS
    pkg_version CONSTANT INTEGER := 1;
    FUNCTION version RETURN INTEGER DETERMINISTIC;
    -- UNIX_TIMESTAMP(date)
    FUNCTION UNIX_TIMESTAMP(in_date IN VARCHAR2) RETURN NUMBER DETERMINISTIC;
    FUNCTION UNIX_TIMESTAMP(in_date IN TIMESTAMP) RETURN NUMBER DETERMINISTIC;
    FUNCTION UNIX_TIMESTAMP(in_date IN DATE) RETURN NUMBER DETERMINISTIC;
END DBMAIL_UTILS;
/
SHOW ERRORS;

CREATE OR REPLACE PACKAGE BODY DBMAIL_UTILS
IS
    FUNCTION VERSION RETURN INTEGER
    IS
    BEGIN
        RETURN pkg_version;
    END;

-- UNIX_TIMESTAMP(varchar2);
    FUNCTION UNIX_TIMESTAMP(in_date IN VARCHAR2) RETURN NUMBER DETERMINISTIC
    IS
    BEGIN
        -- system TZ is assumed to be UTC
        RETURN ROUND( (TO_DATE(in_date, 'YYYY-MM-DD HH24:MI:SS') - TO_DATE('1970-01-01', 'YYYY-MM-DD') ) * 86400);
    END;

-- UNIX_TIMESTAMP(timestamp);
    FUNCTION UNIX_TIMESTAMP(in_date IN TIMESTAMP) RETURN NUMBER DETERMINISTIC
    IS
    BEGIN
        -- system TZ is assumed to be UTC
        RETURN ROUND( (TO_DATE(in_date, 'YYYY-MM-DD HH24:MI:SS') - TO_DATE('1970-01-01', 'YYYY-MM-DD') ) * 86400);
    END;

-- UNIX_TIMESTAMP(date);
    FUNCTION UNIX_TIMESTAMP(in_date IN DATE) RETURN NUMBER DETERMINISTIC
    IS
    BEGIN
        RETURN ROUND( (in_date - TO_DATE('1970-01-01', 'YYYY-MM-DD') ) * 86400);
    END;



END DBMAIL_UTILS;
/
SHOW ERRORS;

