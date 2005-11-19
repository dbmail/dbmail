
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
ALTER TABLE dbmail_users
	ADD maxsieve_size INT8 DEFAULT '0' NOT NULL,
	ADD cursieve_size INT8 DEFAULT '0' NOT NULL;

COMMIT;

