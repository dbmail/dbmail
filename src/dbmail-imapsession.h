/*
 *
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl
 *
 */
	
#ifndef DM_COMMANDCHANNEL_H
#define DM_COMMANDCHANNEL_H

#include "dbmail.h"

#define SESSION_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define SESSION_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }


// command state during idle command
#define IDLE -1 

typedef struct cmd_t *cmd_t;

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

	cmd_t cmd; // command structure (wip)
	gboolean error; // command result
	int error_count;
	ClientState_T state; // session status 
	Connection_T c; // database-connection;
} ImapSession;


typedef int (*IMAP_COMMAND_HANDLER) (ImapSession *);

/* thread data */
typedef struct {
#define DM_THREAD_DATA_MAGIC 0x5af8d
	unsigned int magic;
	Mempool_T pool;
	void (* cb_enter)(gpointer);		/* callback on thread entry		*/
	void (* cb_leave)(gpointer);		/* callback on thread exit		*/
	ImapSession *session;
	gpointer data;                          /* payload */
	volatile int status;			/* command result 			*/
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
int dbmail_imap_session_mailbox_expunge(ImapSession *self);

int dbmail_imap_session_fetch_get_items(ImapSession *self);
int dbmail_imap_session_fetch_parse_args(ImapSession * self);

void dbmail_imap_session_bodyfetch_free(ImapSession *self);

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

