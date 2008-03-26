/*

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

/** 
 * \file dbmailtypes.h
 *
 * all data type definitions used within the dbmail package should 
 * be declared here.
 *
 */

#ifndef _DBMAILTYPES_H
#define _DBMAILTYPES_H

#include "dbmail.h"

/** max length of search query */
#define MAX_SEARCH_LEN 1024

#define MIME_FIELD_MAX 128
#define MIME_VALUE_MAX 4096

#define UID_SIZE 70
#define IPNUM_LEN 32
#define IPLEN 32
#define BACKLOG 16


#define DM_SOCKADDR_LEN 108
#define DM_USERNAME_LEN 100

/** string length of configuration values */
#define FIELDSIZE 1024

/** use 64-bit unsigned integers as common data type */
typedef unsigned long long u64_t;

typedef enum {
	DM_EQUERY 	= -1,
	DM_SUCCESS 	= 0,
	DM_EGENERAL 	= 1
} DbmailErrorCodes;

/** Status fields for messages *
 *
 * Please note that db.c uses 'status < MESSAGE_STATUS_DELETE'
 * and these numbers go into the database as magic values,
 * so don't change them unless you want to break things badly.
 * */
typedef enum {
	MESSAGE_STATUS_NEW     = 0,
	MESSAGE_STATUS_SEEN    = 1,
	MESSAGE_STATUS_DELETE  = 2,
	MESSAGE_STATUS_PURGE   = 3,
	MESSAGE_STATUS_UNUSED  = 4,
	MESSAGE_STATUS_INSERT  = 5,
	MESSAGE_STATUS_ERROR   = 6
} MessageStatus_t;

/** field_t is used for storing configuration values */
typedef char field_t[FIELDSIZE];

/** size of a timestring_t field */
#define TIMESTRING_SIZE 30

/** timestring_t is used for holding timestring */
typedef char timestring_t[TIMESTRING_SIZE];

/** parameters for the database connection */
typedef struct {
	field_t driver;         /**< database driver: mysql, pgsql, sqlite */
	field_t authdriver;     /**< authentication driver: sql, ldap */
	field_t sortdriver;     /**< sort driver: sieve or nothing at all */
	field_t host;		/**< hostname or ip address of database server */
	field_t user;		/**< username to connect with */
	field_t pass;		/**< password of user */
	field_t db;		/**< name of database to connect with */
	unsigned int port;	/**< port number of database server */
	field_t sock;		/**< path to local unix socket (local connection) */
	field_t pfx;		/**< prefix for tables e.g. dbmail_ */
	unsigned int serverid;	/**< unique id for dbmail instance used in clusters */
	field_t encoding;	/**< character encoding to use */
	unsigned int query_time_info;
	unsigned int query_time_message;
	unsigned int query_time_warning;
} db_param_t;

/** configuration items */
typedef struct {
	field_t name;		/**< name of configuration item */
	field_t value;		/**< value of configuration item */
} item_t;
/* dbmail-message */

enum DBMAIL_MESSAGE_CLASS {
	DBMAIL_MESSAGE,
	DBMAIL_MESSAGE_PART
};

typedef enum DBMAIL_MESSAGE_FILTER_TYPES { 
	DBMAIL_MESSAGE_FILTER_FULL = 1,
	DBMAIL_MESSAGE_FILTER_HEAD,
	DBMAIL_MESSAGE_FILTER_BODY
} message_filter_t;

typedef enum DBMAIL_STREAM_TYPE {
	DBMAIL_STREAM_PIPE = 1,
	DBMAIL_STREAM_LMTP,
	DBMAIL_STREAM_RAW
} dbmail_stream_t;

typedef struct {
	u64_t id;
	u64_t physid;
	time_t internal_date;
	int *internal_date_gmtoff;
	GString *envelope_recipient;
	enum DBMAIL_MESSAGE_CLASS klass;
	GMimeObject *content;
	GRelation *headers;
	GHashTable *header_dict;
	GTree *header_name;
	GTree *header_value;
	gchar *charset;
	int part_key;
	int part_depth;
	int part_order;
} DbmailMessage;

/**********************************************************************
 *                              POP3
**********************************************************************/
/** 
 * all POP3 commands */
typedef enum {
	POP3_QUIT,
	POP3_USER,
	POP3_PASS,
	POP3_STAT,
	POP3_LIST,
	POP3_RETR,
	POP3_DELE,
	POP3_NOOP,
	POP3_LAST,
	POP3_RSET,
	POP3_UIDL,
	POP3_APOP,
	POP3_AUTH,
	POP3_TOP,
	POP3_CAPA,
} Pop3Cmd_t;


/** all virtual_ definitions are session specific
 *  when a RSET occurs all will be set to the real values */
struct message {
	u64_t msize;	  /**< message size */
	u64_t messageid;  /**< messageid (from database) */
	u64_t realmessageid; /**< ? */
	char uidl[UID_SIZE]; /**< unique id */
	MessageStatus_t messagestatus;
	MessageStatus_t virtual_messagestatus;
};

/**
 * pop3 connection states */
typedef enum {
	POP3_AUTHORIZATION_STATE,
	POP3_TRANSACTION_STATE,
	POP3_UPDATE_STATE,
	POP3_QUIT_STATE
} Pop3State_t;

/**
 * struct for a POP3 session. Also used for LMTP session.
 */

typedef struct {
	int rx, tx;			/* read and write filehandles */
	struct event_base *base;	/* event base */
	struct bufferevent *rev, *wev;  /* read bufferevent, write bufferevent */
	void (*cb_read) (void *);	// pointers to event callbacks
	void (*cb_time) (void *);
	void (*cb_write) (void *);

	char ip_src[IPNUM_LEN];		/* client IP-number */
	field_t clientname;		/* resolved client hostname */
	int timeout;			/* server timeout (seconds) */
	int login_timeout;		/* login timeout (seconds) */
	int service_before_smtp;

	size_t len;			/* octets read during last ci_read/ci_readln */
	GString *line_buffer;		/* buffer for ci_readln */
} clientinfo_t;

typedef struct {
	clientinfo_t *ci;
	int state;			/**< session state */

	int error_count;		/**< number of errors that have occured */
	int was_apop;			/**< 1 if session was  session was apop (no plaintext password) */
	int SessionResult;		/**< what happened during the session */
	int timeout;			/**< timeout on socket */

	int parser_state;
	int command_state;
	int command_type;		/* command type */
	GList *args;			/* command args (allocated char *) */

	GString *rbuff;			/* input buffer */
	size_t rbuff_size;		/* required number of octets (for string literal) */

	char *username;
	char *password;
	char hostname[64];
	char *apop_stamp;		/**< timestamp for APOP */

	u64_t useridnr;			/**< Used by timsieved */
	u64_t totalsize;		/**< total size of messages */
	u64_t virtual_totalsize;
	u64_t totalmessages; 		/**< number of messages */
	u64_t virtual_totalmessages;

	GList *messagelst;		/** list of messages */
	GList *from;			// lmtp senders
	GList *rcpt;			// lmtp recipients
} ClientSession_t;

typedef struct {
	int no_daemonize;
	int log_verbose;
	char *pidFile;
	int timeout;
	int login_timeout;
	char **iplist; // Allocated memory.
	int ipcount;
	int *listenSockets; // Allocated memory.
	int service_before_smtp;
	int port;
	int backlog;
	int resolveIP;
	field_t service_name;
	field_t serverUser, serverGroup;
	field_t socket;
	field_t log, error_log;
	field_t pid_dir;
	int (*ClientHandler) (clientinfo_t *);
} serverConfig_t;



/**********************************************************************
 *                               IMAP
 *********************************************************************/


/*
 * define some IMAP symbols
 */

enum IMAP_COMMAND_TYPES { 
	IMAP_COMM_NONE,
	IMAP_COMM_CAPABILITY, 
	IMAP_COMM_NOOP, 
	IMAP_COMM_LOGOUT,
	IMAP_COMM_AUTH, 
	IMAP_COMM_LOGIN,
	IMAP_COMM_SELECT, 
	IMAP_COMM_EXAMINE, 
	IMAP_COMM_CREATE,
	IMAP_COMM_DELETE, 
	IMAP_COMM_RENAME, 
	IMAP_COMM_SUBSCRIBE,
	IMAP_COMM_UNSUBSCRIBE, 
	IMAP_COMM_LIST, 
	IMAP_COMM_LSUB,
	IMAP_COMM_STATUS, 
	IMAP_COMM_APPEND,
	IMAP_COMM_CHECK, 
	IMAP_COMM_CLOSE, 
	IMAP_COMM_EXPUNGE,
	IMAP_COMM_SEARCH, 
	IMAP_COMM_FETCH, 
	IMAP_COMM_STORE,
	IMAP_COMM_COPY, 
	IMAP_COMM_UID, 
	IMAP_COMM_SORT,
	IMAP_COMM_GETQUOTAROOT, 
	IMAP_COMM_GETQUOTA,
	IMAP_COMM_SETACL, 
	IMAP_COMM_DELETEACL, 
	IMAP_COMM_GETACL,
	IMAP_COMM_LISTRIGHTS, 
	IMAP_COMM_MYRIGHTS,
	IMAP_COMM_NAMESPACE, 
	IMAP_COMM_THREAD, 
	IMAP_COMM_UNSELECT,
	IMAP_COMM_IDLE,
	IMAP_COMM_LAST
};


enum IMAP4_CLIENT_STATES { 
	IMAPCS_INITIAL_CONNECT,
	IMAPCS_NON_AUTHENTICATED,
	IMAPCS_AUTHENTICATED, 
	IMAPCS_SELECTED, 
	IMAPCS_LOGOUT,
	IMAPCS_DONE,
	IMAPCS_ERROR
};

enum IMAP4_FLAGS { 
	IMAPFLAG_SEEN		= 0x01, 
	IMAPFLAG_ANSWERED	= 0x02,
	IMAPFLAG_DELETED	= 0x04, 
	IMAPFLAG_FLAGGED	= 0x08,
	IMAPFLAG_DRAFT		= 0x10, 
	IMAPFLAG_RECENT		= 0x20
};

typedef enum {
	IMAP_FLAG_SEEN,
	IMAP_FLAG_ANSWERED,
	IMAP_FLAG_DELETED,
	IMAP_FLAG_FLAGGED,
	IMAP_FLAG_DRAFT,
	IMAP_FLAG_RECENT
} imap_flag_t;


enum IMAP4_PERMISSION { 
	IMAPPERM_READ		= 0x01,
	IMAPPERM_READWRITE	= 0x02 
};

enum IMAP4_FLAG_ACTIONS { 
	IMAPFA_NONE, 
	IMAPFA_REPLACE, 
	IMAPFA_ADD,
	IMAPFA_REMOVE
};

enum BODY_FETCH_ITEM_TYPES { 
	BFIT_TEXT, 
	BFIT_HEADER, 
	BFIT_MIME,
	BFIT_HEADER_FIELDS,
	BFIT_HEADER_FIELDS_NOT, 
	BFIT_TEXT_SILENT
};

/* maximum size of a mailbox name */
#define IMAP_MAX_MAILBOX_NAMELEN 255

/* length of internaldate string 
 * DD-MMM-YYYY hh:mm:ss +HHMM
 * 12345678901234567890123456 */
#define IMAP_INTERNALDATE_LEN 27

/* length of database date string 
   YYYY-MM-DD HH:MM:SS
   1234567890123456789 */
#define SQL_INTERNALDATE_LEN 19

/* max length of number/dots part specifier */
#define IMAP_MAX_PARTSPEC_LEN 100

/*
 * search data types
 */

enum IMAP_SEARCH_TYPES { 
	IST_SET = 1, 		/* 1 */
	IST_UIDSET, 		/* 2 */
	IST_FLAG,  		/* 3 */
	IST_SORT,  		/* 4 */
	IST_HDR,  		/* 5 */
	IST_HDRDATE_BEFORE,  	/* 6 */
	IST_HDRDATE_ON,  	/* 7 */
	IST_HDRDATE_SINCE, 	/* 8 */
	IST_IDATE,  		/* 9 */
	IST_DATA_BODY,  	/* 10 */
	IST_DATA_TEXT, 		/* 11 */
	IST_SIZE_LARGER,  	/* 12 */
	IST_SIZE_SMALLER,  	/* 13 */
	IST_SUBSEARCH_AND, 	/* 14 */
	IST_SUBSEARCH_OR,  	/* 15 */
	IST_SUBSEARCH_NOT 	/* 16 */
};

typedef enum {
	SEARCH_UNORDERED = 0,
	SEARCH_SORTED,
	SEARCH_THREAD_ORDEREDSUBJECT,
	SEARCH_THREAD_REFERENCES
} search_order_t;

typedef struct {
	int type;
	u64_t size;
	char table[MAX_SEARCH_LEN];
	char order[MAX_SEARCH_LEN];
	char field[MAX_SEARCH_LEN];
	char search[MAX_SEARCH_LEN];
	char hdrfld[MIME_FIELD_MAX];
	int match;
	GTree *found;
	gboolean reverse;
	gboolean searched;
	gboolean merged;
} search_key_t;



typedef struct {
	int itemtype;		/* the item to be fetched */
	int argstart;		/* start index in the arg array */
	int argcnt;		/* number of args belonging to this bodyfetch */
	guint64 octetstart, octetcnt;	/* number of octets to be retrieved */

	char partspec[IMAP_MAX_PARTSPEC_LEN];	/* part specifier (i.e. '2.1.3' */

	gchar *hdrnames;
	gchar *hdrplist;
	GTree *headers;
} body_fetch_t;


typedef struct {
	GList *bodyfetch;

	gboolean noseen;		/* set the seen flag ? */
	gboolean msgparse_needed;
	gboolean hdrparse_needed;

	/* helpers */
	gboolean setseen;
	gboolean isfirstfetchout;

	/* fetch elements */
	gboolean getUID;
	gboolean getSize;
	gboolean getFlags;
	gboolean getInternalDate;
	gboolean getEnvelope;
	gboolean getMIME_IMB;
	gboolean getMIME_IMB_noextension;
	gboolean getRFC822Header;
	gboolean getRFC822Text;
	gboolean getRFC822Peek;
	gboolean getRFC822;
	gboolean getBodyTotal;
	gboolean getBodyTotalPeek;
} fetch_items_t;

/************************************************************************ 
 *                      simple cache mechanism
 ***********************************************************************/
/* 
 * cached mailbox info
 */
typedef struct {
	// map dbmail_mailboxes
	u64_t uid;
	u64_t msguidnext;
	u64_t owner_idnr;
	char *name;
	time_t mtime;
	unsigned no_select;
	unsigned no_children;
	unsigned no_inferiors;
	unsigned exists;
	unsigned recent;
	unsigned unseen;
	int permission;
	// 
	gboolean is_public;
	gboolean is_users;
	gboolean is_inbox;
	// reference dbmail_keywords
	GList *keywords;
} MailboxInfo;

/*
 * cached message info
 */
#define IMAP_NFLAGS 6
typedef struct {
	// map dbmail_messages
	u64_t id;
	u64_t mailbox_id;
	u64_t rfcsize;
	int flags[IMAP_NFLAGS];
	char internaldate[IMAP_INTERNALDATE_LEN];
	// reference dbmail_keywords
	GList *keywords;
} MessageInfo;


typedef struct {
	u64_t id;
	u64_t rows;		// total number of messages in mailbox
	u64_t recent;
	u64_t unseen;
	u64_t owner_id;
	u64_t size;

	gchar *name;

	GList *sorted;		// ordered list of UID values

	MailboxInfo *info;	// cache mailbox metadata;
	GTree *msginfo; 	// cache MessageInfo

	GTree *ids; 		// key: uid, value: msn
	GTree *msn; 		// key: msn, value: uid

	GNode *search;
	gchar *charset;		// charset used during search/sort

	fetch_items_t *fi;	// imap fetch

	gboolean uid, no_select, no_inferiors, no_children;

} DbmailMailbox;


/*************************************************************************
*                                 SIEVE
*************************************************************************/

/*
 * A struct to hold info about a Sieve script
 */
typedef struct ssinfo {
	char *name;
	int active;
} sievescript_info_t;
/*
 * A struct to say which Sieve allocations
 * will need an associated free.
 */
typedef struct {
	int free_interp  : 1; // t
	int free_script  : 1; // s
	int free_support : 1; // p
	int free_error   : 1; // e
	int free_message : 1; // m
	int free_action  : 1; // a
} sievefree_t;


#define SA_KEEP		1
#define SA_DISCARD	2
#define SA_REDIRECT	3
#define SA_REJECT	4
#define SA_FILEINTO	5

typedef struct sort_action {
	int method;
	char *destination;
	char *message;
} sort_action_t;

typedef enum {
	ACL_RIGHT_LOOKUP,
	ACL_RIGHT_READ,
	ACL_RIGHT_SEEN,
	ACL_RIGHT_WRITE,
	ACL_RIGHT_INSERT,
	ACL_RIGHT_POST,
	ACL_RIGHT_CREATE,
	ACL_RIGHT_DELETE,
	ACL_RIGHT_ADMINISTER,
	ACL_RIGHT_NONE
} ACLRight_t;

struct  ACLMap {
	int lookup_flag;
	int read_flag;
	int seen_flag;
	int write_flag;
	int insert_flag;
	int post_flag;
	int create_flag;
	int delete_flag;
	int administer_flag;
};


/* Depending upon where the mailbox spec comes from,
 * we may or may not create it on the fly and auto-subscribe
 * to it. Some of these will resolve to the same action;
 * see db_find_create_mailbox in db.c to find which.
 */
typedef enum {
	BOX_NONE,        /* No mailbox yet. */
	BOX_UNKNOWN,     /* Not gonna create. */
	BOX_ADDRESSPART, /* Not gonna create. */
	BOX_BRUTEFORCE,  /* Autocreate, no perms checks and skip Sieve scripts. */
	BOX_COMMANDLINE, /* Autocreate. */
	BOX_SORTING,     /* Autocreate. */
	BOX_DEFAULT      /* Autocreate. */
} mailbox_source_t;

typedef enum {
	SQL_TO_DATE,
	SQL_TO_DATETIME,
	SQL_TO_UNIXEPOCH,
	SQL_TO_CHAR,
	SQL_CURRENT_TIMESTAMP,
	SQL_EXPIRE,
	SQL_BINARY,
	SQL_SENSITIVE_LIKE,
	SQL_INSENSITIVE_LIKE,
	SQL_ENCODE_ESCAPE,
	SQL_STRCASE,
	SQL_PARTIAL,
	SQL_IGNORE,
	SQL_RETURNING
} sql_fragment_t;
#endif
