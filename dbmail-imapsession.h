/*
 *
 * Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 *
 */
	
#ifndef _DBMAIL_COMMANDCHANNEL_H
#define _DBMAIL_COMMANDCHANNEL_H

#include "dbmail.h"

/* ImapSession definition */
struct ImapSession {
	clientinfo_t *ci;
	gboolean use_uid;
	u64_t msg_idnr;  // replace this with a GList
	GMimeStream *fstream; // gmime filter wrapper around the TX handler in clientinfo_t
	char *tag;
	char *command;
	char **args;
	fetch_items_t *fi;
	struct DbmailMessage *message;
	struct DbmailMailbox *mailbox;
	GTree *fetch_ids;
	GTree *headers;
	GTree *envelopes;
	GList *recent;	// todo: replace with a struct DbmailMailbox
	GTree *msginfo;
};

typedef int (*IMAP_COMMAND_HANDLER) (struct ImapSession *);

struct ImapSession * dbmail_imap_session_new(void);
struct ImapSession * dbmail_imap_session_setClientinfo(struct ImapSession * self, clientinfo_t *ci);
struct ImapSession * dbmail_imap_session_setTag(struct ImapSession * self, char * tag);
struct ImapSession * dbmail_imap_session_setCommand(struct ImapSession * self, char * command);
struct ImapSession * dbmail_imap_session_setArgs(struct ImapSession * self, char ** args);
struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self);


void dbmail_imap_session_delete(struct ImapSession * self);

int dbmail_imap_session_readln(struct ImapSession * self, char * buffer);
int dbmail_imap_session_discard_to_eol(struct ImapSession *self);

/* \return
 *   -1 on possibly recoverable errors
 *   -2 on serious unrecoverable errors
 */
int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...);

int dbmail_imap_session_set_state(struct ImapSession *self, int state);
int check_state_and_args(struct ImapSession * self, const char * command, int minargs, int maxargs, int state);
int dbmail_imap_session_handle_auth(struct ImapSession * self, char * username, char * password);
int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value);
u64_t dbmail_imap_session_mailbox_get_idnr(struct ImapSession * self, const char * mailbox);
int dbmail_imap_session_mailbox_check_acl(struct ImapSession * self, u64_t idnr, ACLRight_t right);
int dbmail_imap_session_mailbox_get_selectable(struct ImapSession * self, u64_t idnr);
int dbmail_imap_session_mailbox_show_info(struct ImapSession * self);
int dbmail_imap_session_mailbox_open(struct ImapSession * self, const char * mailbox);

int dbmail_imap_session_mailbox_select_recent(struct ImapSession *self);
int dbmail_imap_session_mailbox_update_recent(struct ImapSession *self);

int dbmail_imap_session_fetch_parse_args(struct ImapSession * self, int idx);
int dbmail_imap_session_fetch_get_unparsed(struct ImapSession *self);
int dbmail_imap_session_fetch_get_items(struct ImapSession *self);

void dbmail_imap_session_bodyfetch_new(struct ImapSession *self);
void dbmail_imap_session_bodyfetch_free(struct ImapSession *self);
body_fetch_t * dbmail_imap_session_bodyfetch_get_last(struct ImapSession *self);
void dbmail_imap_session_bodyfetch_rewind(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_partspec(struct ImapSession *self, char *partspec, int length);
char *dbmail_imap_session_bodyfetch_get_last_partspec(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_itemtype(struct ImapSession *self, int itemtype);
int dbmail_imap_session_bodyfetch_get_last_itemtype(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argstart(struct ImapSession *self, int idx);
int dbmail_imap_session_bodyfetch_get_last_argstart(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argcnt(struct ImapSession *self, int idx);
int dbmail_imap_session_bodyfetch_get_last_argcnt(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetstart(struct ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(struct ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetcnt(struct ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(struct ImapSession *self);

char **build_args_array_ext(struct ImapSession *self, const char *originalString);

#endif

