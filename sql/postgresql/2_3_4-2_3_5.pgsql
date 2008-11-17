
alter table dbmail_mailboxes add seq bigint default 0;
alter table dbmail_mailboxes drop mtime;

