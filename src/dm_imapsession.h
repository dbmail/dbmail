/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
 */
	
#ifndef DM_COMMANDCHANNEL_H
#define DM_COMMANDCHANNEL_H

#include "dbmail.h"

// command state during idle command
#define IDLE -1 

/* ImapSession definition */
typedef struct {
	Mempool_T pool;
	pthread_mutex_t lock;
	ClientBase_T *ci;
	Capa_T preauth_capa;   // CAPABILITY
	Capa_T capa;           // CAPABILITY
	char tag[16];
	char command[16];
	int command_type;
	int command_state;

	gboolean use_uid;
	uint64_t msg_idnr;

	String_T buff;         // output buffer

	int parser_state;
	String_T *args;
	uint64_t args_idx;

	int loop;              // IDLE loop counter

	fetch_items *fi;       // FETCH
	qresync_args qresync; // SELECT ... (QRESYNC ...)
	search_order order;    // SORT/SEARCH

	DbmailMailbox *mailbox; // currently selected mailbox
	uint64_t lo;            // lower boundary for message ids
	uint64_t hi;            // upper boundary for message ids
	uint64_t ceiling;       // upper boundary during prefetching

	DbmailMessage *message;

	uint64_t userid;		/* userID of client in dbase */

	GTree *ids;
	GList *new_ids; // store new uids after a COPY command
	GTree *physids;		// cache physmessage_ids for uids 
	GTree *envelopes;
	GTree *mbxinfo; 	// cache MailboxState_T 
	GList *ids_list;

	struct cmd_t *cmd; // command structure (wip)
	gboolean error; // command result
	int error_count;
	ClientState_T state; // session status 
	ImapEnabled_T enabled; // qresync/condstore enabled
	Connection_T c; // database-connection;
} ImapSession;


typedef int (*IMAP_COMMAND_HANDLER) (ImapSession *);

/* thread data */
typedef struct {
#define DM_THREAD_DATA_MAGIC 0x5af8d
	unsigned int magic;
	Mempool_T pool;
	void (* cb_enter)(gpointer);	/* callback on thread entry		*/
	void (* cb_leave)(gpointer);	/* callback on thread exit		*/
	ImapSession *session;
	gpointer data;                  /* payload */
	volatile int status;		/* command result 			*/
} dm_thread_data;

/* public methods */

ImapSession * dbmail_imap_session_new(Mempool_T);
ImapSession * dbmail_imap_session_set_command(ImapSession * self, const char * command);

void dbmail_imap_session_reset(ImapSession *session);

void dbmail_imap_session_args_free(ImapSession *self, gboolean all);
void dbmail_imap_session_fetch_free(ImapSession *self, gboolean all);
void dbmail_imap_session_delete(ImapSession ** self);

void dbmail_imap_session_buff_clear(ImapSession *self);
void dbmail_imap_session_buff_flush(ImapSession *self);
int dbmail_imap_session_buff_printf(ImapSession * self, char * message, ...);

int dbmail_imap_session_set_state(ImapSession *self, ClientState_T state);
int dbmail_imap_session_handle_auth(ImapSession * self, const char * username, const char * password);

MailboxState_T dbmail_imap_session_mbxinfo_lookup(ImapSession *self, uint64_t mailbox_idnr);

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update);
int dbmail_imap_session_mailbox_expunge(ImapSession *self, const char *set, uint64_t *modseq);

int dbmail_imap_session_fetch_get_items(ImapSession *self);
int dbmail_imap_session_fetch_parse_args(ImapSession * self);

void dbmail_imap_session_bodyfetch_free(ImapSession *self);

int imap4_tokenizer_main(ImapSession *self, const char *buffer);


void _ic_cb_leave(gpointer data);

#endif

