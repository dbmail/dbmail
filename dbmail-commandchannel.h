
#ifndef _DBMAIL_COMMANDCHANNEL_H
#define _DBMAIL_COMMANDCHANNEL_H

#include "dbmailtypes.h"
#include "acl.h"
#include "misc.h"
#include "auth.h"
#include "imap4.h"
#include "glib.h"

/* ImapCommandChannel definition */
struct ImapCommandChannel {
	ClientInfo *ci;
	char *tag;
	char *command;
	char **args;
	fetch_items_t *fi;
	mime_message_t *headermsg;
	msginfo_t *msginfo;
};

typedef int (*IMAP_COMMAND_HANDLER) (struct ImapCommandChannel *);

struct ImapCommandChannel * ic_new(void);
void ic_setClientInfo(struct ImapCommandChannel * self, ClientInfo *ci);
void ic_setTag(struct ImapCommandChannel * self, char * tag);
void ic_setArgs(struct ImapCommandChannel * self, char ** args);

void ic_delete(struct ImapCommandChannel * self);

int ic_write(struct ImapCommandChannel * self, char * message, ...);
int ic_handle_auth(struct ImapCommandChannel * self, char * username, char * password);
int ic_check_state_and_args(struct ImapCommandChannel * self, const char * command, 
		int minargs, int maxargs, int state);
int ic_prompt(struct ImapCommandChannel * self, char * prompt, char * value);
u64_t ic_mailbox_get_idnr(struct ImapCommandChannel * self, char * mailbox);
int ic_mailbox_check_acl(struct ImapCommandChannel * self, u64_t idnr, ACLRight_t right);
int ic_mailbox_get_selectable(struct ImapCommandChannel * self, u64_t idnr);
int ic_mailbox_show_info(struct ImapCommandChannel * self);
int ic_mailbox_open(struct ImapCommandChannel * self, char * mailbox);

int ic_fetch_parse_args(struct ImapCommandChannel * self, int idx);
int ic_fetch_get_unparsed(struct ImapCommandChannel *self, u64_t fetch_start, u64_t fetch_end);
#endif

