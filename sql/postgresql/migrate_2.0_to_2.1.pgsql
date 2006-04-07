
BEGIN;
create cast (text as bytea) without function;
alter table dbmail_messageblks add blk_bytea bytea;
update dbmail_messageblks set blk_bytea = cast(messageblk::text as bytea);
alter table dbmail_messageblks drop column messageblk;
alter table dbmail_messageblks rename blk_bytea TO messageblk;
drop cast (text as bytea);
COMMIT;


