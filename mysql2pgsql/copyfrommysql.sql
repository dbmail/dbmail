# loads all data from mysql tables into temporary files

SELECT * FROM users INTO OUTFILE '/tmp/users.mysqldata';
SELECT * FROM aliases INTO OUTFILE '/tmp/aliases.mysqldata';
SELECT * FROM mailboxes INTO OUTFILE '/tmp/mailboxes.mysqldata';
SELECT * FROM messageblks INTO OUTFILE '/tmp/messageblks.mysqldata';
SELECT * FROM messages INTO OUTFILE '/tmp/messages.mysqldata';
