CREATE SEQUENCE seq_pbsp_id;
CREATE TABLE pbsp (
  idnr BIGINT NOT NULL DEFAULT NEXTVAL('seq_pbsp_id'),
  since TIMESTAMP NOT NULL DEFAULT '1970-01-01 00:00:00',
  ipnumber VARCHAR(40) NOT NULL DEFAULT '',
  PRIMARY KEY (idnr)
);
CREATE INDEX idx_ipnumber ON pbsp (ipnumber);
CREATE INDEX idx_since ON pbsp (since);
