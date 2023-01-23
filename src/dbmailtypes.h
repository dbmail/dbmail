/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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

#ifndef DBMAILTYPES_H
#define DBMAILTYPES_H

#include "dbmail.h"
#include "dm_mempool.h"

// start worker threads per client
//#define DM_CLIENT_THREADS

/** max length of search query */
#define MAX_SEARCH_LEN 2048

#define MIME_FIELD_MAX 128
#define MIME_VALUE_MAX 4096

#define MAXSOCKETS 256

#define UID_SIZE 96
#define IPNUM_LEN 32
#define IPLEN 32
#define BACKLOG 128

#define DM_SOCKADDR_LEN 108
#define DM_USERNAME_LEN 255

/** string length of configuration values */
#define FIELDSIZE 1024

/** Maximal size available for comparision CLOB with string constant inside
 * simple SELECT ... WHERE ... statement */
#define DM_ORA_MAX_BYTES_LOB_CMP 4000

typedef enum {
	DM_DRIVER_SQLITE	= 1,
	DM_DRIVER_MYSQL		= 2,
	DM_DRIVER_POSTGRESQL	= 3,
	DM_DRIVER_ORACLE	= 4
} Driver_T;

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
} MessageStatus_T;

/** Field_T is used for storing configuration values */
typedef char Field_T[FIELDSIZE];

/** size of a TimeString_T field */
#define TIMESTRING_SIZE 30

/** TimeString_T is used for holding timestring */
typedef char TimeString_T[TIMESTRING_SIZE];

/** parameters for the database connection */
typedef struct {
	Field_T dburi;
	Driver_T db_driver; // 
	Field_T driver;         /**< database driver: mysql, pgsql, sqlite */
	Field_T authdriver;     /**< authentication driver: sql, ldap */
	Field_T sortdriver;     /**< sort driver: sieve or nothing at all */
	Field_T host;		/**< hostname or ip address of database server */
	Field_T user;		/**< username to connect with */
	Field_T pass;		/**< password of user */
	Field_T db;		/**< name of database to connect with */
	unsigned int port;	/**< port number of database server */
	Field_T sock;		/**< path to local unix socket (local connection) */
	Field_T pfx;		/**< prefix for tables e.g. dbmail_ */
	unsigned int max_db_connections; /**< maximum connections the pool will create with the database */
	unsigned int serverid;	/**< unique id for dbmail instance used in clusters */
	Field_T encoding;	/**< character encoding to use */
	unsigned int query_time_info;
	unsigned int query_time_notice;
	unsigned int query_time_warning;
	unsigned int query_timeout;
} DBParam_T;

enum DBMAIL_MESSAGE_CLASS {
	DBMAIL_MESSAGE,
	DBMAIL_MESSAGE_PART
};

enum DBMAIL_MESSAGE_FILTER_TYPES { 
	DBMAIL_MESSAGE_FILTER_FULL = 1,
	DBMAIL_MESSAGE_FILTER_HEAD,
	DBMAIL_MESSAGE_FILTER_BODY
};

typedef struct {
	// Memory Pool
	Mempool_T pool;
	gboolean freepool;

	// ID
	uint64_t id;
	uint64_t msg_idnr;
	
	// scanned values
	const char *charset;
	time_t internal_date;
	int internal_date_gmtoff;
	String_T envelope_recipient;
	
	// Data access
	enum DBMAIL_MESSAGE_CLASS klass;
	GMimeObject *content;
	GMimeStream *stream;
	String_T crlf; 

	// Mappings
	GHashTable *header_dict;
	GTree *header_name;
	GTree *header_value;
	
	// storage 
	int part_key;
	int part_depth;
	int part_order;

} DbmailMessage;

/**********************************************************************
 *                              POP3
**********************************************************************/

struct message {
	uint64_t msize;	  /**< message size */
	uint64_t messageid;  /**< messageid (from database) */
	uint64_t realmessageid; /**< ? */
	char uidl[UID_SIZE]; /**< unique id */
	MessageStatus_T messagestatus;
	MessageStatus_T virtual_messagestatus;
};


/**********************************************************************
 *                              IMAP
**********************************************************************/

enum IMAP_COMMAND_TYPES { 
	IMAP_COMM_NONE,                 // 0
	IMAP_COMM_CAPABILITY,           // 1 
	IMAP_COMM_NOOP,                 // 2
	IMAP_COMM_LOGOUT,               // 3
	IMAP_COMM_AUTH,                 // 4
	IMAP_COMM_LOGIN,                // 5
	IMAP_COMM_SELECT,               // 6
	IMAP_COMM_EXAMINE,              // 7
	IMAP_COMM_ENABLE,               // 8
	IMAP_COMM_CREATE,               // 9
	IMAP_COMM_DELETE,               // 10
	IMAP_COMM_RENAME,               // 11
	IMAP_COMM_SUBSCRIBE,            // 12
	IMAP_COMM_UNSUBSCRIBE,          // 13
	IMAP_COMM_LIST,                 // 14
	IMAP_COMM_LSUB,                 // 15
	IMAP_COMM_STATUS,               // 16
	IMAP_COMM_APPEND,               // 17
	IMAP_COMM_CHECK,                // 18
	IMAP_COMM_CLOSE,                // 19
	IMAP_COMM_EXPUNGE,              // 20
	IMAP_COMM_SEARCH,               // 21
	IMAP_COMM_FETCH,                // 22
	IMAP_COMM_STORE,                // 23
	IMAP_COMM_COPY,                 // 24
	IMAP_COMM_UID,                  // 25
	IMAP_COMM_SORT,                 // 26
	IMAP_COMM_GETQUOTAROOT,         // 27
	IMAP_COMM_GETQUOTA,             // 28
	IMAP_COMM_SETACL,               // 29
	IMAP_COMM_DELETEACL,            // 30
	IMAP_COMM_GETACL,               // 31
	IMAP_COMM_LISTRIGHTS,           // 32
	IMAP_COMM_MYRIGHTS,             // 33
	IMAP_COMM_NAMESPACE,            // 34
	IMAP_COMM_THREAD,               // 35
	IMAP_COMM_UNSELECT,             // 36
	IMAP_COMM_IDLE,                 // 37
	IMAP_COMM_STARTTLS,             // 38
	IMAP_COMM_ID,                   // 39
	IMAP_COMM_LAST                  // 40
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
} ClientState_T;

typedef struct {
	unsigned int condstore : 1;
	unsigned int qresync   : 1;
} ImapEnabled_T;

enum {
	IMAP_FLAG_SEEN,
	IMAP_FLAG_ANSWERED,
	IMAP_FLAG_DELETED,
	IMAP_FLAG_FLAGGED,
	IMAP_FLAG_DRAFT,
	IMAP_FLAG_RECENT
};

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
	BFIT_TEXT               = 1, 
	BFIT_HEADER             = 2, 
	BFIT_MIME               = 3,
	BFIT_HEADER_FIELDS      = 4,
	BFIT_HEADER_FIELDS_NOT  = 5, 
	BFIT_ALL                = 6
};


// thread manager structures 
//

// client_thread
typedef struct  {
	Mempool_T pool;
	int sock;
	SSL *ssl;                       /* SSL/TLS context for this client */
	gboolean ssl_state;		/* SSL_accept done or not */
	struct sockaddr caddr;
	socklen_t caddr_len;
	struct sockaddr saddr;
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
	Mempool_T pool;
	client_sock *sock;
	int rx, tx;                     /* read and write filehandles */
	uint64_t bytes_rx;		/* read byte counter */
	uint64_t bytes_tx;		/* write byte counter */

	pthread_mutex_t lock;
	int client_state;		/* CLIENT_OK, CLIENT_AGAIN, CLIENT_EOF */
	int deferred;                   // deferred cleanup counter

	struct event *pev;		/* self-pipe event */
	void (*cb_pipe) (void *);	/* callback for self-pipe events */

	struct event *rev, *wev;  	/* read event, write event */
	void (*cb_time) (void *);
	void (*cb_write) (void *);
	int (*cb_error) (int fd, int error, void *);

	Cram_T auth;                    /* authentication context for cram-md5 */
	uint64_t authlog_id;

	Field_T clientname;             /* resolved client name */

	char src_ip[NI_MAXHOST+1];	/* client IP-number */
	char src_port[NI_MAXSERV+1];	/* client port number */

	char dst_ip[NI_MAXHOST+1];	/* server IP-number */
	char dst_port[NI_MAXSERV+1];      /* server port number */

	struct timeval timeout;		/**< timeout on socket */

	int service_before_smtp;

	char tls_wbuf[TLS_SEGMENT];	/* buffer to write during tls session */
	uint64_t tls_wbuf_n;		/* number of octets to write during tls session */

	uint64_t rbuff_size;              /* size of string-literals */
	String_T read_buffer;		/* input buffer */
	uint64_t read_buffer_offset;	/* input buffer offset */

	String_T write_buffer;		/* output buffer */
	uint64_t write_buffer_offset;	/* output buffer offset */

	uint64_t len;			/* crlf decoded octets read by last ci_read(ln) call */
} ClientBase_T;

struct http_sock {
	char address[128];
	unsigned short port;
};

typedef struct {
	Mempool_T pool;
	ClientBase_T *ci;
	ClientState_T state;			/**< session state */
	void (*handle_input) (void *);

	int error_count;		/**< number of errors that have occured */
	int was_apop;			/**< 1 if session was  session was apop (no plaintext password) */
	int SessionResult;		/**< what happened during the session */

	int parser_state;
	int command_state;
	int command_type;		/* command type */
	List_T args;			/* command args (allocated char *) */

	String_T rbuff;			/* input buffer */

	char *username;
	char *password;
	char hostname[64];
	char *apop_stamp;		/**< timestamp for APOP */

	uint64_t useridnr;			/**< Used by timsieved */
	uint64_t totalsize;		/**< total size of messages */
	uint64_t virtual_totalsize;
	uint64_t totalmessages; 		/**< number of messages */
	uint64_t virtual_totalmessages;

	List_T messagelst;		/** list of messages */
	List_T from;			// lmtp senders
	List_T rcpt;			// lmtp recipients
} ClientSession_T;

typedef struct {
	int no_daemonize;
	int log_verbose;
	char *pidFile;
	int timeout;
	int login_timeout;
	char **iplist;                  // Allocated memory.
	Field_T port;
	Field_T ssl_port;
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
	struct evhttp **evhs;           // http server sockets list
	Field_T service_name;
        Field_T process_name;
	Field_T serverUser;
        Field_T serverGroup;
	Field_T socket;
	Field_T log;
	Field_T error_log;
	Field_T pid_dir;
        Field_T tls_cafile;
        Field_T tls_cert;
        Field_T tls_key;
        Field_T tls_ciphers;
	int (*ClientHandler) (client_sock *);
	void (*cb) (struct evhttp_request *, void *);
	GTree *security_actions;
} ServerConfig_T;



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
#define SQL_INTERNALDATE_LEN 32

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
} search_order;

typedef struct {
	int type;
	uint64_t size;
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
} search_key;



typedef struct {
	int itemtype;		/* the item to be fetched */
	int argstart;		/* start index in the arg array */
	int argcnt;		/* number of args belonging to this bodyfetch */
	guint64 octetstart, octetcnt;	/* number of octets to be retrieved */

	char partspec[IMAP_MAX_PARTSPEC_LEN];	/* part specifier (i.e. '2.1.3' */

	gchar *hdrnames;
	gchar *hdrplist;
	GTree *headers;
	GList *names;
} body_fetch;


typedef struct {
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

	/* condstore */
	uint64_t changedsince;
	/* qresync */
	gboolean vanished;

	List_T   bodyfetch;
} fetch_items;

typedef struct {
	uint64_t uidvalidity;
	uint64_t modseq;
	String_T known_uids;
	String_T known_seqset;
	String_T known_uidset;
} qresync_args;


/************************************************************************ 
 *                      simple cache mechanism
 ***********************************************************************/

/*
 * cached message info
 */
#define IMAP_NFLAGS 6
typedef struct { // map dbmail_messages
	uint64_t mailbox_id;
	uint64_t msn;
	uint64_t uid;
	uint64_t rfcsize;
	uint64_t seq;
	// physmessage_id
	uint64_t phys_id;
        // expunge flag
        int expunge;
        // expunged (pushed to client), can be removed
        int expunged;
	int status;
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
} sievescript_info;
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
} sievefree;


#define SA_KEEP		1
#define SA_DISCARD	2
#define SA_REDIRECT	3
#define SA_REJECT	4
#define SA_FILEINTO	5

typedef struct sort_action {
	int method;
	char *destination;
	char *message;
} sort_action;

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
} ACLRight;

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
	BOX_DEFAULT,     /* Autocreate. */
	BOX_IMAP         /* Autocreate, no subscribe */
} mailbox_source;

typedef enum {
	SQL_TO_DATE,
	SQL_TO_DATETIME,
	SQL_TO_UNIXEPOCH,
	SQL_TO_CHAR,
	SQL_CURRENT_TIMESTAMP,
	SQL_EXPIRE,
	SQL_WITHIN,
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
} sql_fragment;
#endif
