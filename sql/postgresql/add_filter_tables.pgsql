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

