/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/* $Id$ */

/* Add an ACL table to the database */

CREATE SEQUENCE acl_id_seq;
CREATE TABLE acl (
    id INT8 DEFAULT nextval('acl_id_seq'),
    user_id INT8 NOT NULL,
    mailbox_id INT8 NOT NULL,
    lookup_flag INT2 DEFAULT '0' NOT NULL,
    read_flag INT2 DEFAULT '0' NOT NULL,
    seen_flag INT2 DEFAULT '0' NOT NULL,
    write_flag INT2 DEFAULT '0' NOT NULL,
    insert_flag INT2 DEFAULT '0' NOT NULL,
    post_flag INT2 DEFAULT '0' NOT NULL,
    create_flag INT2 DEFAULT '0' NOT NULL,
    delete_flag INT2 DEFAULT '0' NOT NULL,
    administer_flag INT2 DEFAULT '0' NOT NULL,
    PRIMARY KEY (id),
    FOREIGN KEY (user_id) REFERENCES users(user_idnr) ON DELETE CASCADE,
    FOREIGN KEY (mailbox_id) REFERENCES mailboxes(mailbox_idnr) ON DELETE CASCADE
);
CREATE INDEX acl_user_mailbox_idx ON acl(user_id, mailbox_id);

