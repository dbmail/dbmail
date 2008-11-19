alter table dbmail_mailboxes add seq bigint default 0;
alter table dbmail_mailboxes drop mtime;

alter table dbmail_users alter column passwd varchar(130) not null;
