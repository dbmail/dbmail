
BEGIN;

ALTER TABLE ONLY dbmail_headervalue DROP CONSTRAINT dbmail_headervalue_physmessage_id_fkey;
ALTER TABLE ONLY dbmail_headervalue DROP CONSTRAINT dbmail_headervalue_headername_id_fkey;
DROP INDEX dbmail_headervalue_3;
DROP INDEX dbmail_headervalue_2;
DROP INDEX dbmail_headervalue_1;
DROP TABLE dbmail_headervalue CASCADE;

CREATE SEQUENCE dbmail_headervalue_id_seq;
CREATE TABLE dbmail_headervalue (
        id            INT8 NOT NULL DEFAULT nextval('dbmail_headervalue_id_seq'),
	hash          varchar(256) NOT NULL,
        headervalue   TEXT NOT NULL DEFAULT '',
        PRIMARY KEY (id)
);

CREATE INDEX dbmail_headervalue_1 ON dbmail_headervalue USING btree (hash);

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

begin;
delete from dbmail_ccfield;
delete from dbmail_datefield;
delete from dbmail_fromfield;
delete from dbmail_referencesfield;
delete from dbmail_replycache;
delete from dbmail_replytofield;
delete from dbmail_subjectfield;
delete from dbmail_tofield;
commit;
BEGIN;
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

