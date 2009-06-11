BEGIN;
ALTER TABLE dbmail_acl ADD COLUMN deleted_flag INT2 DEFAULT '0' NOT NULL;
ALTER TABLE dbmail_acl ADD COLUMN expunge_flag INT2 DEFAULT '0' NOT NULL;
UPDATE dbmail_acl SET deleted_flag=delete_flag, expunge_flag=delete_flag;
COMMIT;

BEGIN;
DROP TABLE IF EXISTS dbmail_auto_replies;
DROP TABLE IF EXISTS dbmail_auto_notifications;
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
        SELECT physmessage_id,datefield
        FROM dbmail_messages m
        JOIN dbmail_header h USING (physmessage_id)
        JOIN dbmail_headername n ON h.headername_id = n.id
        JOIN dbmail_headervalue v ON h.headervalue_id = v.id
WHERE n.headername='date';


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

