/*

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl

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

// start worker threads per client
//#define DM_CLIENT_THREADS

/** max length of search query */
#define MAX_SEARCH_LEN 2048

#define MIME_FIELD_MAX 128
#define MIME_VALUE_MAX 4096

#define MAXSOCKETS 256

#define UID_SIZE 70
#define IPNUM_LEN 32
#define IPLEN 32
#define BACKLOG 16

#define DM_SOCKADDR_LEN 108
#define DM_USERNAME_LEN 100

/** string length of configuration values */
#define FIELDSIZE 1024

/** Maximal size available for comparision CLOB with string constant inside
 * simple SELECT ... WHERE ... statement */
#define DM_ORA_MAX_BYTES_LOB_CMP 4000

/** use 64-bit unsigned integers as common data type */
typedef unsigned long long u64_t;

typedef enum {
	DM_DRIVER_SQLITE	= 1,
	DM_DRIVER_MYSQL		= 2,
	DM_DRIVER_POSTGRESQL	= 3,
	DM_DRIVER_ORACLE	= 4
} dm_driver_t;

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
	dm_driver_t	db_driver; // 
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
	unsigned int max_db_connections; /**< maximum connections the pool will create with the database */
	unsigned int serverid;	/**< unique id for dbmail instance used in clusters */
	field_t encoding;	/**< character encoding to use */
	unsigned int query_time_info;
	unsigned int query_time_notice;
	unsigned int query_time_warning;
	unsigned int query_timeout;
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

typedef struct {
	u64_t id;
	u64_t physid;
	time_t internal_date;
	int internal_date_gmtoff;
	GString *envelope_recipient;
	enum DBMAIL_MESSAGE_CLASS klass;
	GMimeObject *content;
	gchar *raw_content;
	GRelation *headers;
	GHashTable *header_dict;
	GTree *header_name;
	GTree *header_value;
	gchar *charset;
	int part_key;
	int part_depth;
	int part_order;
	FILE *tmp;
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
	POP3_STLS,
	POP3_FAIL
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

/*
 * define some IMAP symbols
 */

enum IMAP_COMMAND_TYPES { 
	IMAP_COMM_NONE,			// 0
	IMAP_COMM_CAPABILITY, 		// 1 
	IMAP_COMM_NOOP, 		// 2
	IMAP_COMM_LOGOUT,		// 3
	IMAP_COMM_AUTH, 		// 4
	IMAP_COMM_LOGIN,		// 5
	IMAP_COMM_SELECT, 		// 6
	IMAP_COMM_EXAMINE, 		// 7
	IMAP_COMM_CREATE,		// 8
	IMAP_COMM_DELETE, 		// 9
	IMAP_COMM_RENAME, 		// 10
	IMAP_COMM_SUBSCRIBE,		// 11
	IMAP_COMM_UNSUBSCRIBE, 		// 12
	IMAP_COMM_LIST, 		// 13
	IMAP_COMM_LSUB,			// 14
	IMAP_COMM_STATUS, 		// 15
	IMAP_COMM_APPEND,		// 16
	IMAP_COMM_CHECK, 		// 17
	IMAP_COMM_CLOSE, 		// 18
	IMAP_COMM_EXPUNGE,		// 19
	IMAP_COMM_SEARCH, 		// 20
	IMAP_COMM_FETCH, 		// 21
	IMAP_COMM_STORE,		// 22
	IMAP_COMM_COPY, 		// 23
	IMAP_COMM_UID, 			// 24
	IMAP_COMM_SORT,			// 25
	IMAP_COMM_GETQUOTAROOT, 	// 26
	IMAP_COMM_GETQUOTA,		// 27
	IMAP_COMM_SETACL, 		// 28
	IMAP_COMM_DELETEACL, 		// 29
	IMAP_COMM_GETACL,		// 30
	IMAP_COMM_LISTRIGHTS, 		// 31
	IMAP_COMM_MYRIGHTS,		// 32
	IMAP_COMM_NAMESPACE, 		// 33
	IMAP_COMM_THREAD, 		// 34
	IMAP_COMM_UNSELECT,		// 35
	IMAP_COMM_IDLE,			// 36
	IMAP_COMM_STARTTLS,		// 37
	IMAP_COMM_ID,			// 38
	IMAP_COMM_LAST			// 39
};


typedef enum { 
	CLIENTSTATE_ANY 			= -1,
	CLIENTSTATE_INITIAL_CONNECT		= 0,
	CLIENTSTATE_NON_AUTHENTICATED		= 1,
	CLIENTSTATE_AUTHENTICATED		= 2,
	CLIENTSTATE_SELECTED			= 3,
	CLIENTSTATE_LOGOUT			= 4,
	CLIENTSTATE_QUIT			= 5,
	CLIENTSTATE_ERROR			= 6,
	CLIENTSTATE_QUIT_QUEUED			= 7
} clientstate_t;

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


// thread manager structures 
//

// client_thread
typedef struct  {
	int sock;
	SSL *ssl;                       /* SSL/TLS context for this client */
	gboolean ssl_state;		/* SSL_accept done or not */
	struct sockaddr *caddr;
	socklen_t caddr_len;
	struct sockaddr *saddr;
	socklen_t saddr_len;
	void (*cb_close) (void *);	/* termination callback */
} client_sock;

//

#define TLS_SEGMENT	262144
#define CLIENT_OK	0
#define CLIENT_AGAIN	1
#define CLIENT_ERR	2
#define CLIENT_EOF	4

typedef struct {
	int rx, tx;			/* read and write filehandles */
	u64_t bytes_rx;			/* read byte counter */
	u64_t bytes_tx;			/* write byte counter */
	SSL *ssl;                       /* SSL/TLS context for this client */
	gboolean ssl_state;		/* SSL_accept done or not */
	int client_state;		/* CLIENT_OK, CLIENT_AGAIN, CLIENT_EOF */

	struct event *pev;		/* self-pipe event */
	void (*cb_pipe) (void *);	/* callback for self-pipe events */

	struct event *rev, *wev;  	/* read event, write event */
	void (*cb_time) (void *);
	void (*cb_write) (void *);
	int (*cb_error) (int fd, int error, void *);

	Cram_T auth;                    /* authentication context for cram-md5 */
	u64_t authlog_id;

	field_t clientname;             /* resolved client name */

	char src_ip[NI_MAXHOST];	/* client IP-number */
	char src_port[NI_MAXSERV];	/* client port number */

	char dst_ip[NI_MAXHOST];	/* server IP-number */
	char dst_port[NI_MAXSERV];      /* server port number */

	struct timeval *timeout;	/**< timeout on socket */

	int service_before_smtp;

	char tls_wbuf[TLS_SEGMENT];	/* buffer to write during tls session */
	size_t tls_wbuf_n;		/* number of octets to write during tls session */

	size_t rbuff_size;              /* size of string-literals */
	GString *read_buffer;		/* input buffer */
	size_t read_buffer_offset;	/* input buffer offset */

	GString *write_buffer;		/* output buffer */
	size_t write_buffer_offset;	/* output buffer offset */

	size_t len;			/* crlf decoded octets read by last ci_read(ln) call */
} clientbase_t;

struct http_sock {
	char address[128];
	unsigned short port;
};

typedef struct {
	clientbase_t *ci;
	clientstate_t state;			/**< session state */
	void (*handle_input) (void *);

	int error_count;		/**< number of errors that have occured */
	int was_apop;			/**< 1 if session was  session was apop (no plaintext password) */
	int SessionResult;		/**< what happened during the session */

	int parser_state;
	int command_state;
	int command_type;		/* command type */
	GList *args;			/* command args (allocated char *) */

	GString *rbuff;			/* input buffer */

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
	char **iplist;                  // Allocated memory.
	field_t port;
	field_t ssl_port;
	int ipcount;
	int socketcount;
	int ssl_socketcount;
	int *listenSockets;             // Allocated memory.
	int *ssl_listenSockets;         // Allocated memory.
	int service_before_smtp;
	gboolean authlog;
	gboolean ssl;
	int backlog;
	int resolveIP;
	struct evhttp *evh;		// http server
	field_t service_name, process_name;
	field_t serverUser, serverGroup;
	field_t socket;
	field_t log, error_log;
	field_t pid_dir;
        field_t tls_cafile;
        field_t tls_cert;
        field_t tls_key;
        field_t tls_ciphers;
	int (*ClientHandler) (client_sock *);
	void (*cb) (struct evhttp_request *, void *);
} serverConfig_t;



/**********************************************************************
 *                               IMAP
 *********************************************************************/

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
	IST_SET = 1,
	IST_UIDSET,
	IST_FLAG,
	IST_KEYWORD,
	IST_UNKEYWORD,
	IST_SORT,
	IST_HDR,
	IST_HDRDATE_BEFORE,
	IST_HDRDATE_ON,
	IST_HDRDATE_SINCE,
	IST_IDATE,
	IST_DATA_BODY,
	IST_DATA_TEXT,
	IST_SIZE_LARGER,
	IST_SIZE_SMALLER,
	IST_SUBSEARCH_AND,
	IST_SUBSEARCH_OR,
	IST_SUBSEARCH_NOT
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
	char op[MAX_SEARCH_LEN];
	char search[MAX_SEARCH_LEN];
	char hdrfld[MIME_FIELD_MAX];
//	int match;
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
 * cached message info
 */
#define IMAP_NFLAGS 6
typedef struct { // map dbmail_messages
	u64_t mailbox_id;
	u64_t msn;
	u64_t uid;
	u64_t rfcsize;
	char internaldate[IMAP_INTERNALDATE_LEN];
	int flags[IMAP_NFLAGS];
	// reference dbmail_keywords
	GList *keywords;
} MessageInfo;


/*************************************************************************
*                                 SIEVE
*************************************************************************/

/*
 * A struct to hold info about a Sieve script
 */
typedef struct {
	char name[512];
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
	ACL_RIGHT_LOOKUP, // l
	ACL_RIGHT_READ, // r
	ACL_RIGHT_SEEN, // s
	ACL_RIGHT_WRITE, // w
	ACL_RIGHT_INSERT, // i
	ACL_RIGHT_POST, // p
	ACL_RIGHT_CREATE, // k - also set with c
	ACL_RIGHT_DELETE, // x - also set with d
	ACL_RIGHT_DELETED, // t - also set with d
	ACL_RIGHT_EXPUNGE, // e - also set with d
	ACL_RIGHT_ADMINISTER, // a
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
	int deleted_flag;
	int expunge_flag;
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
	SQL_RETURNING,
	SQL_TABLE_EXISTS,
	SQL_ESCAPE_COLUMN,
	SQL_COMPARE_BLOB
} sql_fragment_t;
#endif
