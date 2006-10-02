
BEGIN;
CREATE CAST (text AS bytea) WITHOUT FUNCTION;
ALTER TABLE dbmail_messageblks ADD blk_bytea bytea;
UPDATE dbmail_messageblks SET blk_bytea = CAST(messageblk::text AS bytea);
ALTER TABLE dbmail_messageblks DROP COLUMN messageblk;
ALTER TABLE dbmail_messageblks RENAME blk_bytea TO messageblk;
ALTER TABLE dbmail_messageblks ALTER messageblk SET not null;
DROP CAST (text AS bytea);
COMMIT;



BEGIN;
alter table dbmail_auto_replies add start_date timestamp without time zone;
alter table dbmail_auto_replies alter start_date set not null;
alter table dbmail_auto_replies add stop_date timestamp without time zone;
alter table dbmail_auto_replies alter stop_date set not null;
COMMIT;

--
-- modify dbmail schema to support header caching.
--
-- $Id: add_header_tables.psql 1634 2005-03-07 16:13:21Z paul $
--
--
-- store all headers by storing all headernames and headervalues in separate
-- tables.
--

BEGIN TRANSACTION;

CREATE SEQUENCE dbmail_headername_idnr_seq;
CREATE TABLE dbmail_headername (
	id		INT8 DEFAULT nextval('dbmail_headername_idnr_seq'),
	headername	VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_headername_1 on dbmail_headername(headername);


CREATE SEQUENCE dbmail_headervalue_idnr_seq;
CREATE TABLE dbmail_headervalue (
	headername_id	INT8 NOT NULL
		REFERENCES dbmail_headername(id)
		ON UPDATE CASCADE ON DELETE CASCADE,
        physmessage_id	INT8 NOT NULL
		REFERENCES dbmail_physmessage(id)
		ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_headervalue_idnr_seq'),
	headervalue	TEXT NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_headervalue_1 ON dbmail_headervalue(physmessage_id, id);


-- provide separate storage of commonly used headers

-- Threading
-- support fast threading by breaking out In-Reply-To and References headers
-- these fields contain zero or more Message-Id values that determine the message
-- threading

CREATE SEQUENCE dbmail_subjectfield_idnr_seq;
CREATE TABLE dbmail_subjectfield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_subjectfield_idnr_seq'),
	subjectfield	VARCHAR(255) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_subjectfield_1 ON dbmail_subjectfield(physmessage_id, id);


CREATE SEQUENCE dbmail_datefield_idnr_seq;
CREATE TABLE dbmail_datefield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_datefield_idnr_seq'),
	datefield	TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT '1970-01-01 00:00:00',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_datefield_1 ON dbmail_datefield(physmessage_id, id);

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


-- Searching and Sorting


-- support fast sorting by breaking out and preparsing the fields most commonly used
-- in searching and sorting: From, To, Reply-To, Cc,


CREATE SEQUENCE dbmail_fromfield_idnr_seq;
CREATE TABLE dbmail_fromfield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_fromfield_idnr_seq'),
	fromname	VARCHAR(100) NOT NULL DEFAULT '',
	fromaddr	VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_fromfield_1 ON dbmail_fromfield(physmessage_id, id);

CREATE SEQUENCE dbmail_tofield_idnr_seq;
CREATE TABLE dbmail_tofield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_tofield_idnr_seq'),
	toname		VARCHAR(100) NOT NULL DEFAULT '',
	toaddr		VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_tofield_1 ON dbmail_tofield(physmessage_id, id);

CREATE SEQUENCE dbmail_replytofield_idnr_seq;
CREATE TABLE dbmail_replytofield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_replytofield_idnr_seq'),
	replytoname	VARCHAR(100) NOT NULL DEFAULT '',
	replytoaddr	VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_replytofield_1 ON dbmail_replytofield(physmessage_id, id);

CREATE SEQUENCE dbmail_ccfield_idnr_seq;
CREATE TABLE dbmail_ccfield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_ccfield_idnr_seq'),
	ccname		VARCHAR(100) NOT NULL DEFAULT '',
	ccaddr		VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_ccfield_1 ON dbmail_ccfield(physmessage_id, id);

-- Some other fields will also be commonly used for search/sort but do not warrant 
-- preparsing and/or separate tables. 

--ALTER TABLE dbmail_physmessage 
--	ADD sendername 	VARCHAR(100) NOT NULL DEFAULT '';
--	
--ALTER TABLE dbmail_physmessage 
--	ADD senderaddr 	VARCHAR(100) NOT NULL DEFAULT '';
--
--ALTER TABLE dbmail_physmessage 
--	ADD subject 	VARCHAR(255) NOT NULL DEFAULT '';
--
--ALTER TABLE dbmail_physmessage 
--	ADD messageid 	VARCHAR(100) NOT NULL DEFAULT '';

COMMIT;

DROP TABLE dbmail_replycache;
CREATE TABLE dbmail_replycache (
    to_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    from_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    handle    character varying(100) DEFAULT ''::character varying,
    lastseen timestamp without time zone NOT NULL
);
CREATE UNIQUE INDEX replycache_1 ON dbmail_replycache USING btree (to_addr, from_addr, handle);

--
-- Add tables and columns to hold Sieve scripts.
--
-- $Id: add_header_tables.mysql 1634 2005-03-07 16:13:21Z paul $
--

BEGIN TRANSACTION;

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

-- Looking in db.c, the WHERE clauses are: owner, owner name, owner active.
CREATE INDEX dbmail_sievescripts_1 on dbmail_sievescripts(owner_idnr,name);
CREATE INDEX dbmail_sievescripts_2 on dbmail_sievescripts(owner_idnr,active);


-- Add columns for storing the Sieve quota.
ALTER TABLE dbmail_users ADD maxsieve_size INT8;
UPDATE dbmail_users SET maxsieve_size=0;
ALTER TABLE dbmail_users ALTER maxsieve_size SET NOT NULL;
ALTER TABLE dbmail_users ALTER maxsieve_size SET DEFAULT '0';

ALTER TABLE dbmail_users ADD cursieve_size INT8;
UPDATE dbmail_users SET cursieve_size=0;
ALTER TABLE dbmail_users ALTER cursieve_size SET NOT NULL;
ALTER TABLE dbmail_users ALTER cursieve_size SET DEFAULT '0';

COMMIT;


CREATE TABLE dbmail_usermap(
  login VARCHAR(100) NOT NULL,
  sock_allow VARCHAR(100) NOT NULL,
  sock_deny VARCHAR(100) NOT NULL,
  userid VARCHAR(100) NOT NULL
);

CREATE UNIQUE INDEX usermap_idx_1 ON dbmail_usermap(login, sock_allow, userid);

CREATE SEQUENCE dbmail_envelope_idnr_seq;
CREATE TABLE dbmail_envelope (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_envelope_idnr_seq'),
	envelope	TEXT NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_envelope_1 ON dbmail_envelope(physmessage_id, id);


