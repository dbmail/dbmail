/*
 *
 * Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
 *
 */
	
#ifndef _DBMAIL_COMMANDCHANNEL_H
#define _DBMAIL_COMMANDCHANNEL_H

#include "dbmail.h"

#define ERROR -1
#define DONE 1

/*
 * cached raw message data
 */
typedef struct {
	DbmailMessage *dmsg;
	MEM *memdump;
	MEM *tmpdump;
	u64_t num;
	u64_t dumpsize;
	int file_dumped;
	int msg_parsed;
} cache_t;


/* ImapSession definition */
typedef struct {
	clientinfo_t *ci;
	u64_t msg_idnr;  // replace this with a GList

	GString *buff; // output buffer

	int parser_state;
	int command_state;
	char *rbuff; // input buffer
	int rbuff_size;

	gboolean use_uid;
	char *tag;
	char *command;
	int command_type;
	int timeout;
	int loop; // idle loop counter
	char **args;
	u64_t args_idx;
	fetch_items_t *fi;

	DbmailMailbox *mailbox;	/* currently selected mailbox */
	u64_t lo; // lower boundary for message ids
	u64_t hi; // upper boundary for message ids
	u64_t ceiling; // upper boundary during prefetching

	// FIXME: there is too much redundancy here
	DbmailMessage *message;
	cache_t *cached_msg;

	u64_t userid;		/* userID of client in dbase */

	GTree *ids;
	GTree *envelopes;
	GTree *mbxinfo; // cache MailboxInfo
	GList *recent;
	GList *ids_list;
	gpointer cmd; // command structure
	gboolean error; // command result
	int state; // session status 
	int error_count;
	void (*cb_read)(void *);
	void (*cb_time)(void *);
	
} ImapSession;

typedef struct {
	gboolean silent;
	int action;
	int flaglist[IMAP_NFLAGS];
	GList *keywords;
} cmd_store_t;

typedef struct {
	u64_t mailbox_id;
} cmd_copy_t;


typedef int (*IMAP_COMMAND_HANDLER) (ImapSession *);

/* thread data */
typedef struct {
	char *tag; char *command; char **args;	/* parsed command input 		*/
	gpointer data;				/* payload				*/
	gpointer (* cb_enter)(gpointer);	/* callback on thread entry		*/
	gpointer (* cb_leave)(gpointer);	/* callback on thread exit		*/
	char *result; 				/* allocated output string buffer	*/
	struct bufferevent *wev;		/* bufferevent for sending output	*/
} imap_cmd_t;
	

/* public methods */

ImapSession * dbmail_imap_session_new(void);
ImapSession * dbmail_imap_session_set_tag(ImapSession * self, char * tag);
ImapSession * dbmail_imap_session_set_command(ImapSession * self, char * command);
ImapSession * dbmail_imap_session_reset_fetchitems(ImapSession * self);

void dbmail_imap_session_set_callbacks(ImapSession *self, void *cb_r, void *cb_t, int timeout);
void dbmail_imap_session_reset_callbacks(ImapSession *self);

void dbmail_imap_session_args_free(ImapSession *self, gboolean all);
void dbmail_imap_session_fetch_free(ImapSession *self);
void dbmail_imap_session_delete(ImapSession * self);

int dbmail_imap_session_readln(ImapSession * self, char * buffer);
int dbmail_imap_session_discard_to_eol(ImapSession *self);

void dbmail_imap_session_buff_clear(ImapSession *self);
void dbmail_imap_session_buff_append(ImapSession *self, char *message, ...);
void dbmail_imap_session_buff_flush(ImapSession *self);
int dbmail_imap_session_printf(ImapSession * self, char * message, ...);

int dbmail_imap_session_set_state(ImapSession *self, int state);
int client_is_authenticated(ImapSession * self);
int check_state_and_args(ImapSession * self, int minargs, int maxargs, int state);
int dbmail_imap_session_handle_auth(ImapSession * self, char * username, char * password);
int dbmail_imap_session_prompt(ImapSession * self, char * prompt);


void dbmail_imap_session_get_mbxinfo(ImapSession *self);
MailboxInfo * dbmail_imap_session_mbxinfo_lookup(ImapSession *self, u64_t mailbox_idnr);

u64_t dbmail_imap_session_mailbox_get_idnr(ImapSession * self, const char * mailbox);
int dbmail_imap_session_mailbox_check_acl(ImapSession * self, u64_t idnr, ACLRight_t right);
int dbmail_imap_session_mailbox_get_selectable(ImapSession * self, u64_t idnr);

int dbmail_imap_session_mailbox_status(ImapSession * self, gboolean update);
int dbmail_imap_session_idle(ImapSession *self);
int dbmail_imap_session_mailbox_show_info(ImapSession * self);
int dbmail_imap_session_mailbox_flags(ImapSession * self);
int dbmail_imap_session_mailbox_open(ImapSession * self, const char * mailbox);
int dbmail_imap_session_mailbox_close(ImapSession *self);

int dbmail_imap_session_mailbox_expunge(ImapSession *self);
int dbmail_imap_session_mailbox_select_recent(ImapSession *self);
int dbmail_imap_session_mailbox_update_recent(ImapSession *self);

int dbmail_imap_session_fetch_parse_args(ImapSession * self);
int dbmail_imap_session_fetch_get_items(ImapSession *self);

void dbmail_imap_session_bodyfetch_new(ImapSession *self);
void dbmail_imap_session_bodyfetch_free(ImapSession *self);
body_fetch_t * dbmail_imap_session_bodyfetch_get_last(ImapSession *self);
void dbmail_imap_session_bodyfetch_rewind(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_partspec(ImapSession *self, char *partspec, int length);
char *dbmail_imap_session_bodyfetch_get_last_partspec(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_itemtype(ImapSession *self, int itemtype);
int dbmail_imap_session_bodyfetch_get_last_itemtype(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argstart(ImapSession *self);
int dbmail_imap_session_bodyfetch_get_last_argstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_argcnt(ImapSession *self);
int dbmail_imap_session_bodyfetch_get_last_argcnt(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetstart(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetstart(ImapSession *self);

int dbmail_imap_session_bodyfetch_set_octetcnt(ImapSession *self, guint64 octet);
guint64 dbmail_imap_session_bodyfetch_get_last_octetcnt(ImapSession *self);

int build_args_array_ext(ImapSession *self, const char *originalString);


/* threaded work queue */
	
/* 
 * send the ic->result buffer to the client 
 * default thread-exit callback
 */
gpointer ic_flush(gpointer data);

/*
 * thread launcher
 *
 */
void ic_dispatch(ImapSession *session, gpointer cb_enter, gpointer cb_leave, gpointer data);


#endif

