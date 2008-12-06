ALTER TABLE dbmail_mailboxes ADD seq BIGINT DEFAULT 0;
CREATE INDEX dbmail_mailboxes_seq ON dbmail_mailboxes(seq);
ALTER TABLE dbmail_mailboxes DROP mtime;
ALTER TABLE dbmail_users ALTER COLUMN passwd VARCHAR(130) NOT NULL;
