
BEGIN;
ALTER TABLE dbmail_users ADD COLUMN spasswd VARCHAR(130) DEFAULT '' NOT NULL;
ALTER TABLE dbmail_users ADD COLUMN saction SMALLINT DEFAULT '0' NOT NULL;
ALTER TABLE dbmail_users ADD COLUMN active SMALLINT DEFAULT '1' NOT NULL;

INSERT INTO dbmail_upgrade_steps (from_version, to_version) values (32001, 32004);
COMMIT;
