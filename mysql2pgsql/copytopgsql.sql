/* copies all data from tmp files into postgres */
/*  database should be empty (!) */
/* no other users should access the tables while doing this! */

BEGIN;
COPY users FROM '/tmp/users.mysqldata';
SELECT setval('user_idnr_seq', max(user_idnr)) FROM users;
END;

BEGIN;
COPY aliases FROM '/tmp/aliases.mysqldata';
SELECT setval('alias_idnr_seq', max(alias_idnr)) FROM aliases;
END;

BEGIN;
COPY mailboxes FROM '/tmp/mailboxes.mysqldata';
SELECT setval('mailbox_idnr_seq', max(mailbox_idnr)) FROM mailboxes;
END;

BEGIN;
COPY messageblks FROM '/tmp/messageblks.mysqldata';
SELECT setval('messageblk_idnr_seq', max(messageblk_idnr)) FROM messageblks;
END;

BEGIN;
COPY messages FROM '/tmp/messages.mysqldata';
SELECT setval('message_idnr_seq', max(message_idnr)) FROM messages;
END;




