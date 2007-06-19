
SET client_encoding = 'UTF8';

CREATE TABLE dbmail_mimeparts (
    id character(64) NOT NULL,
    data bytea NOT NULL,
    size bigint NOT NULL
);


ALTER TABLE ONLY dbmail_mimeparts
    ADD CONSTRAINT dbmail_mimeparts_pkey PRIMARY KEY (id);


CREATE TABLE dbmail_partlists (
    physmessage_id bigint NOT NULL,
    is_header smallint DEFAULT (0)::smallint NOT NULL,
    part_key integer DEFAULT 0 NOT NULL,
    part_depth integer DEFAULT 0 NOT NULL,
    part_order integer DEFAULT 0 NOT NULL,
    part_id character(64) NOT NULL
);


CREATE INDEX dbmail_partlists_1 ON dbmail_partlists USING btree (physmessage_id);
CREATE INDEX dbmail_partlists_2 ON dbmail_partlists USING btree (part_id);

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_part_id_fkey FOREIGN KEY (part_id) REFERENCES dbmail_mimeparts(id) ON UPDATE CASCADE ON DELETE CASCADE;

ALTER TABLE ONLY dbmail_partlists
    ADD CONSTRAINT dbmail_partlists_physmessage_id_fkey FOREIGN KEY (physmessage_id) REFERENCES dbmail_physmessage(id) ON UPDATE CASCADE ON DELETE CASCADE;

