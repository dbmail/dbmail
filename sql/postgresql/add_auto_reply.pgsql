
CREATE SEQUENCE auto_reply_seq;
CREATE TABLE auto_replies (
   auto_reply_idnr INT8 DEFAULT nextval('auto_reply_seq'),
   user_idnr INT8 DEFAULT '0' NOT NULL,
   reply_body TEXT
);
