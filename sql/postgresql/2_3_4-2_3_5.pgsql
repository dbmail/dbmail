begin;
ALTER TABLE dbmail_mailboxes ADD seq BIGINT DEFAULT 0;
CREATE INDEX dbmail_mailboxes_seq ON dbmail_mailboxes(seq);
ALTER TABLE dbmail_mailboxes DROP mtime;
alter table dbmail_users add p2 varchar(130) not null default '';
update dbmail_users set p2=passwd;
alter table dbmail_users drop passwd;
alter table dbmail_users rename p2 to passwd;
commit;

