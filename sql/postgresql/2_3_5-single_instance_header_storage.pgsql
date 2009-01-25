
ALTER TABLE ONLY dbmail_headervalue DROP CONSTRAINT dbmail_headervalue_physmessage_id_fkey;
ALTER TABLE ONLY dbmail_headervalue DROP CONSTRAINT dbmail_headervalue_headername_id_fkey;
DROP INDEX dbmail_headervalue_3;
DROP INDEX dbmail_headervalue_2;
DROP INDEX dbmail_headervalue_1;
ALTER TABLE ONLY dbmail_headervalue DROP CONSTRAINT dbmail_headervalue_pkey;
DROP TABLE dbmail_headervalue;
DROP SEQUENSE dbmail_headername_idnr_seq;


CREATE SEQUENCE dbmail_headervalue_id_seq;
CREATE TABLE dbmail_headervalue (
        value_id INT8 NOT NULL DEFAULT nextval('dbmail_headervalue_id_seq'),
        value   TEXT NOT NULL DEFAULT '',
        PRIMARY KEY (value_id)
);
CREATE UNIQUE INDEX dbmail_headervalue_1 ON dbmail_headervalue(value);

DROP INDEX dbmail_headername_1;
ALTER TABLE ONLY dbmail_headername DROP CONSTRAINT dbmail_headername_pkey;
DROP TABLE dbmail_headername;
DROP SEQUENSE dbmail_headervalue_idnr_seq;

CREATE SEQUENCE dbmail_headername_id_seq;
CREATE TABLE dbmail_headername (
        name_id  INT8 NOT NULL DEFAULT nextval('dbmail_headername_id_seq'),
        name    VARCHAR(100) NOT NULL DEFAULT 'BROKEN_HEADER',
        PRIMARY KEY (name_id)
);
CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername(lower(name));


CREATE TABLE dbmail_headernamevalue (
        physmessage_id      INT8 NOT NULL
		REFERENCES dbmail_physmessage(id)
                ON UPDATE CASCADE ON DELETE CASCADE,
        name_id  INT8 NOT NULL
                REFERENCES dbmail_headername(name_id)
                ON UPDATE CASCADE ON DELETE RESTRICT,
        value_id      INT8 NOT NULL
                REFERENCES dbmail_headervalue(value_id)
                ON UPDATE CASCADE ON DELETE RESTRICT,
        PRIMARY KEY (physmessage_id,name_id,value_id)
);

