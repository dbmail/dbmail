
BEGIN;
alter table dbmail_auto_replies add start_date timestamp without time zone;
alter table dbmail_auto_replies alter start_date set not null;
alter table dbmail_auto_replies add stop_date timestamp without time zone;
alter table dbmail_auto_replies alter stop_date set not null;
COMMIT;
