/* $Id$
   use this script to add constraints to PostgreSQL to
   possibly make database functions somewhat faster

   script written after comments by Paul J Stevens on
   dbmail@dbmail.org mailing list
*/
/* add some indices to tables to speed up operations */
CREATE INDEX name_idx ON mailboxes(name);
CREATE INDEX owner_id_idx ON mailboxes(owner_idnr);
CREATE INDEX is_subscribed_idx ON mailboxes(is_subscribed);
CREATE INDEX mailbox_id_idx ON messages(mailbox_idnr);
CREATE INDEX seen_flag_idx ON messages(seen_flag);
CREATE INDEX unique_id_idx ON messages(unique_id);
CREATE INDEX status_idx ON messages(status);

/* add foreign keys to tables */
ALTER TABLE messages ADD FOREIGN KEY (mailbox_idnr) REFERENCES
  mailboxes(mailbox_idnr) ON DELETE CASCADE;
ALTER TABLE mailboxes ADD FOREIGN KEY (owner_idnr) REFERENCES
  users(user_idnr) ON DELETE CASCADE;
ALTER TABLE messageblks ADD FOREIGN KEY (message_idnr) REFERENCES
  messages(message_idnr) ON DELETE CASCADE;
