ALTER TABLE users ADD PRIMARY KEY(user_idnr);
ALTER TABLE mailboxes ADD PRIMARY KEY(mailbox_idnr);
ALTER TABLE messages ADD PRIMARY KEY(message_idnr);
ALTER TABLE messageblks ADD PRIMARY KEY(messageblk_idnr);
ALTER TABLE aliases ADD PRIMARY KEY(alias_idnr);

CREATE INDEX aliases_alias_idx ON aliases(alias);

CREATE UNIQUE INDEX users_id_idx ON users (userid);
CREATE INDEX users_name_idx ON users(userid);

CREATE UNIQUE INDEX mailboxes_id_idx ON mailboxes(mailbox_idnr);
CREATE INDEX mailboxes_owner_idx ON mailboxes(owner_idnr);

CREATE UNIQUE INDEX messages_id_idx ON messages(message_idnr);
CREATE INDEX messages_mailbox_idx ON messages(mailbox_idnr);

CREATE UNIQUE INDEX messageblks_id_idx ON messageblks(messageblk_idnr);
CREATE INDEX messageblks_msg_idx ON messageblks(message_idnr);
