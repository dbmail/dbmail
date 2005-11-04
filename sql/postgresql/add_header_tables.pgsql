
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
	subjectfield	VARCHAR(100) NOT NULL DEFAULT '',
	PRIMARY KEY (id)
);
CREATE UNIQUE INDEX dbmail_subjectfield_1 ON dbmail_subjectfield(physmessage_id, id);


CREATE SEQUENCE dbmail_datefield_idnr_seq;
CREATE TABLE dbmail_datefield (
        physmessage_id  INT8 NOT NULL
			REFERENCES dbmail_physmessage(id)
			ON UPDATE CASCADE ON DELETE CASCADE,
	id		INT8 DEFAULT nextval('dbmail_datefield_idnr_seq'),
	datefield	VARCHAR(100) NOT NULL DEFAULT '',
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

