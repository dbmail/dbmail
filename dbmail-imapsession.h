
#ifndef _DBMAIL_COMMANDCHANNEL_H
#define _DBMAIL_COMMANDCHANNEL_H

#include "dbmailtypes.h"
#include "acl.h"
#include "misc.h"
#include "auth.h"
#include "imap4.h"
#include "glib.h"
#include "dbmail-message.h"

/* ImapSession definition */
struct ImapSession {
	ClientInfo *ci;
	int use_uid;
	u64_t msg_idnr;  // replace this with a GList
	char *tag;
	char *command;
	char **args;
	fetch_items_t fi;
	struct DbmailMessage *message;
	msginfo_t *msginfo;
};

typedef int (*IMAP_COMMAND_HANDLER) (struct ImapSession *);

struct ImapSession * dbmail_imap_session_new(void);
struct ImapSession * dbmail_imap_session_setClientInfo(struct ImapSession * self, ClientInfo *ci);
struct ImapSession * dbmail_imap_session_setTag(struct ImapSession * self, char * tag);
struct ImapSession * dbmail_imap_session_setCommand(struct ImapSession * self, char * command);
struct ImapSession * dbmail_imap_session_setArgs(struct ImapSession * self, char ** args);
struct ImapSession * dbmail_imap_session_setFi(struct ImapSession * self, fetch_items_t fi);
struct ImapSession * dbmail_imap_session_setHeadermsg(struct ImapSession * self, mime_message_t headermsg);
struct ImapSession * dbmail_imap_session_setMsginfo(struct ImapSession * self, msginfo_t * msginfo);
struct ImapSession * dbmail_imap_session_resetFi(struct ImapSession * self);


void dbmail_imap_session_delete(struct ImapSession * self);

int dbmail_imap_session_readln(struct ImapSession * self, char * buffer);
int dbmail_imap_session_printf(struct ImapSession * self, char * message, ...);

int check_state_and_args(struct ImapSession * self, const char * command, int minargs, int maxargs, int state);
int dbmail_imap_session_handle_auth(struct ImapSession * self, char * username, char * password);
int dbmail_imap_session_prompt(struct ImapSession * self, char * prompt, char * value);
u64_t dbmail_imap_session_mailbox_get_idnr(struct ImapSession * self, char * mailbox);
int dbmail_imap_session_mailbox_check_acl(struct ImapSession * self, u64_t idnr, ACLRight_t right);
int dbmail_imap_session_mailbox_get_selectable(struct ImapSession * self, u64_t idnr);
int dbmail_imap_session_mailbox_show_info(struct ImapSession * self);
int dbmail_imap_session_mailbox_open(struct ImapSession * self, char * mailbox);

#endif

