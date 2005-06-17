CREATE TABLE dbmail_replycache (
    to_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    from_addr character varying(100) DEFAULT ''::character varying NOT NULL,
    lastseen timestamp without time zone NOT NULL
);
CREATE UNIQUE INDEX replycache_1 ON dbmail_replycache USING btree (to_addr, from_addr);
