
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

