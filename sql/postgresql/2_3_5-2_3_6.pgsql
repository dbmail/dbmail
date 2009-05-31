
begin;
drop table if exists dbmail_auto_replies;
drop table if exists dbmail_auto_notifications;
drop table if exists dbmail_ccfield;
drop table if exists dbmail_datefield;
drop table if exists dbmail_fromfield;
drop table if exists dbmail_replytofield;
drop table if exists dbmail_subjectfield;
drop table if exists dbmail_tofield;
delete from dbmail_referencesfield;


ALTER TABLE dbmail_acl ADD COLUMN deleted_flag INT2 DEFAULT '0' NOT NULL;
ALTER TABLE dbmail_acl ADD COLUMN expunge_flag INT2 DEFAULT '0' NOT NULL;
UPDATE dbmail_acl SET deleted_flag=delete_flag, expunge_flag=delete_flag;

DROP TABLE if exists dbmail_headervalue CASCADE;
DROP TABLE if exists dbmail_headername CASCADE;
DROP TABLE if exists dbmail_header;

DROP SEQUENCE IF EXISTS dbmail_headername_id_seq;
CREATE SEQUENCE dbmail_headername_id_seq;
CREATE TABLE dbmail_headername (
        id  INT8 NOT NULL DEFAULT nextval('dbmail_headername_id_seq'),
        headername    VARCHAR(100) NOT NULL DEFAULT 'BROKEN_HEADER',
        PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername(lower(headername));

DROP SEQUENCE IF EXISTS dbmail_headervalue_id_seq;
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
COMMIT;

BEGIN;
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
COMMIT;

