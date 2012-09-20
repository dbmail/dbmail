/*
 *
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl
 *
 */
	
#ifndef DM_COMMANDCHANNEL_H
#define DM_COMMANDCHANNEL_H

#include "dbmail.h"
#include "dm_cache.h"

// command state during idle command
#define IDLE -1 

typedef struct cmd_t *cmd_t;

/* ImapSession definition */
typedef struct {
	GMutex lock;
	ClientBase_T *ci;
	Capa_T preauth_capa;   // CAPABILITY
	Capa_T capa;   // CAPABILITY
	char *tag;
	char *command;
	int command_type;
	int command_state;

	gboolean use_uid;
	uint64_t msg_idnr;  // replace this with a GList

	GString *buff; // output buffer

	int parser_state;
	char **args;
	uint64_t args_idx;

	int loop; // idle loop counter
	fetch_items *fi;

	DbmailMailbox *mailbox;	/* currently selected mailbox */
	uint64_t lo; // lower boundary for message ids
	uint64_t hi; // upper boundary for message ids
	uint64_t ceiling; // upper boundary during prefetching

	DbmailMessage *message;
	Cache_T cache;  

	uint64_t userid;		/* userID of client in dbase */

	GTree *ids;
	GTree *physids;		// cache physmessage_ids for uids 
	GTree *envelopes;
	GTree *mbxinfo; 	// cache MailboxState_T 
	GList *ids_list;

	cmd_t cmd; // command structure (wip)
	gboolean error; // command result
	int error_count;
	ClientState_T state; // session status 
	Connection_T c; // database-connection;
} ImapSession;


typedef int (*IMAP_COMMAND_HANDLER) (ImapSession *);

/* thread data */
typedef struct {
	void (* cb_enter)(gpointer);		/* callback on thread entry		*/
	void (* cb_leave)(gpointer);		/* callback on thread exit		*/
	ImapSession *session;
	ClientBase_T ci;
	gpointer data;				/* payload				*/
	int status;				/* command result 			*/
} dm_thread_data;

/* public methods */

ImapSession * dbmail_imap_session_new(void);
ImapSession * dbmail_imap_session_set_tag(ImapSession * self, char * tag);
ImapSession * dbmail_imap_session_set_command(ImapSession * self, char * command);

void dbmail_imap_session_reset(ImapSession *session);

void dbmail_imap_session_args_free(ImapSession *self, gboolean all);
void dbmail_imap_session_fetch_free(ImapSession *self);
void dbmail_imap_session_delete(ImapSession ** self);

void dbmail_imap_session_buff_clear(ImapSession *self);
void dbmail_imap_session_buff_flush(ImapSession *self);
int dbmail_imap_session_buff_printf(ImapSession * self, char * message, ...);

int dbmail_imap_session_set_state(ImapSession *self, ClientState_T state);
int dbmail_imap_session_handle_auth(ImapSession * self, const char * username, const char * password);

MailboxState_T dbmail_imap_session_mbxinfo_lookup(ImapSession *self, uint64_t mailbox_idnr);

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update);
int dbmail_imap_session_mailbox_expunge(ImapSession *self);

int dbmail_imap_session_fetch_get_items(ImapSession *self);
int dbmail_imap_session_fetch_parse_args(ImapSession * self);

void dbmail_imap_session_bodyfetch_new(ImapSession *self);
void dbmail_imap_session_bodyfetch_free(ImapSession *self);
void dbmail_imap_session_bodyfetch_rewind(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length);
int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype);
int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self);
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self);

int imap4_tokenizer_main(ImapSession *self, const char *buffer);


/* threaded work queue */
	
/*
 * thread launcher
 *
 */
void dm_thread_data_push(gpointer session, gpointer cb_enter, gpointer cb_leave, gpointer data);

void dm_thread_data_sendmessage(gpointer data);
void _ic_cb_leave(gpointer data);

#endif

