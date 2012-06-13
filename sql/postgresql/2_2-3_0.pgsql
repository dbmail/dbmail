BEGIN;

DROP INDEX dbmail_sievescripts_1;
CREATE UNIQUE INDEX dbmail_sievescripts_1 ON dbmail_sievescripts(owner_idnr, name);

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

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_part_id_fkey FOREIGN KEY (part_id) REFERENCES dbmail_mimeparts(id) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_physmessage_id_fkey FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage(id) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE ONLY dbmail_mailboxes ALTER COLUMN name TYPE VARCHAR(255);
ALTER TABLE ONLY dbmail_mailboxes ADD mtime TIMESTAMP WITHOUT TIME ZONE;

CREATE TABLE dbmail_keywords (
	message_idnr bigint NOT NULL,
	keyword varchar(64) NOT NULL
);
ALTER TABLE ONLY dbmail_keywords
    ADD CONSTRAINT dbmail_keywords_pkey PRIMARY KEY (message_idnr, keyword);
ALTER TABLE ONLY dbmail_keywords
    ADD CONSTRAINT dbmail_keywords_fkey FOREIGN KEY (message_idnr) REFERENCES dbmail_messages (message_idnr) ON DELETE CASCADE ON UPDATE CASCADE;

ALTER TABLE dbmail_mailboxes ADD seq BIGINT DEFAULT 0;
CREATE INDEX dbmail_mailboxes_seq ON dbmail_mailboxes(seq);
ALTER TABLE dbmail_mailboxes DROP mtime;
alter table dbmail_users add p2 varchar(130) not null default '';
update dbmail_users set p2=passwd;
alter table dbmail_users drop passwd;
alter table dbmail_users rename p2 to passwd;

ALTER TABLE dbmail_acl ADD COLUMN deleted_flag INT2 DEFAULT '0' NOT NULL;
ALTER TABLE dbmail_acl ADD COLUMN expunge_flag INT2 DEFAULT '0' NOT NULL;
UPDATE dbmail_acl SET deleted_flag=delete_flag, expunge_flag=delete_flag;

DROP TABLE IF EXISTS dbmail_ccfield;
DROP SEQUENCE IF EXISTS dbmail_ccfield_idnr_seq;
DROP TABLE IF EXISTS dbmail_datefield;
DROP SEQUENCE IF EXISTS dbmail_datefield_idnr_seq;
DROP TABLE IF EXISTS dbmail_fromfield;
DROP SEQUENCE IF EXISTS dbmail_fromfield_idnr_seq;
DROP TABLE IF EXISTS dbmail_replytofield;
DROP SEQUENCE IF EXISTS dbmail_replytofield_idnr_seq;
DROP TABLE IF EXISTS dbmail_subjectfield;
DROP SEQUENCE IF EXISTS dbmail_subjectfield_idnr_seq;
DROP TABLE IF EXISTS dbmail_tofield;
DROP SEQUENCE IF EXISTS dbmail_tofield_idnr_seq;
DELETE FROM dbmail_referencesfield;


DROP TABLE IF EXISTS dbmail_filters;
DROP SEQUENCE IF EXISTS dbmail_filters_id_seq;
CREATE SEQUENCE dbmail_filters_id_seq;
CREATE TABLE dbmail_filters (
	user_id      INT8 REFERENCES dbmail_users(user_idnr) ON DELETE CASCADE ON UPDATE CASCADE,
	id           INT8 NOT NULL DEFAULT nextval('dbmail_filters_id_seq'),
	headername   varchar(128) NOT NULL,
	headervalue  varchar(255) NOT NULL,	
	mailbox      varchar(100) NOT NULL,	
	PRIMARY KEY (user_id, id)
);

CREATE UNIQUE INDEX message_parts ON dbmail_partlists(physmessage_id, part_key, part_depth, part_order);

DROP TABLE IF EXISTS dbmail_authlog;
DROP SEQUENCE IF EXISTS dbmail_authlog_id_seq;
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


DROP TABLE if exists dbmail_header;

DROP TABLE if exists dbmail_headername CASCADE;
DROP SEQUENCE IF EXISTS dbmail_headername_id_seq;
DROP SEQUENCE IF EXISTS dbmail_headername_idnr_seq;
CREATE SEQUENCE dbmail_headername_id_seq;
CREATE TABLE dbmail_headername (
        id  INT8 NOT NULL DEFAULT nextval('dbmail_headername_id_seq'),
        headername    VARCHAR(100) NOT NULL DEFAULT 'BROKEN_HEADER',
        PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername(lower(headername));

DROP TABLE if exists dbmail_headervalue CASCADE;
DROP SEQUENCE IF EXISTS dbmail_headervalue_id_seq;
DROP SEQUENCE IF EXISTS dbmail_headervalue_idnr_seq;
CREATE SEQUENCE dbmail_headervalue_id_seq;
CREATE TABLE dbmail_headervalue (
        id            INT8 NOT NULL DEFAULT nextval('dbmail_headervalue_id_seq'),
        hash          VARCHAR(256) NOT NULL,
        headervalue   TEXT NOT NULL DEFAULT '',
        sortfield     VARCHAR(255) DEFAULT NULL,
        datefield     TIMESTAMP WITHOUT TIME ZONE,
        PRIMARY KEY (id)
);
CREATE INDEX dbmail_headervalue_1 ON dbmail_headervalue USING btree (hash);
CREATE INDEX dbmail_headervalue_2 ON dbmail_headervalue USING btree (sortfield);
CREATE INDEX dbmail_headervalue_3 ON dbmail_headervalue USING btree (datefield);

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

