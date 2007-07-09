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
//typedef unsigned long long u64_t;
#define u64_t guint64

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

typedef struct {
	FILE *tx, *rx;
	char ip_src[IPNUM_LEN];	/* client IP-number */
	field_t clientname;	/* resolved client ip */
	int timeout;		/* server timeout (seconds) */
	int login_timeout;	/* login timeout (seconds) */
	void *userData;
} clientinfo_t;

typedef struct {
	int maxConnect;
	int *listenSockets;
	int numSockets;
	int resolveIP;
	int timeout;
	int login_timeout;
	int (*ClientHandler) (clientinfo_t *);
} ChildInfo_t;


/*
 * structures used by POP mechanism
 *
 */

/** all virtual_ definitions are session specific
 *  when a RSET occurs all will be set to the real values */
struct message {
	u64_t msize;	  /**< message size */
	u64_t messageid;  /**< messageid (from database) */
	u64_t realmessageid;
			  /**< ? */
	char uidl[UID_SIZE];
			  /**< unique id */
	/* message status :
	 * 000 message is new, never touched 
	 * 001 message is read
	 * 002 message is deleted by user 
	 * ----------------------------------
	 * The server additionally uses:
	 * 003 message is deleted by sysop
	 * 004 message is ready for final deletion */

	/** message status */
	MessageStatus_t messagestatus;
	/** virtual message status */
	MessageStatus_t virtual_messagestatus;
};

/**
 * pop3 connection states */
typedef enum {
	POP3_AUTHORIZATION_STATE,
	POP3_TRANSACTION_STATE,
	POP3_UPDATE_STATE,
} Pop3State_t;

/**
 * struct for a POP3 session.
 */
typedef struct {
	int error_count;/**< number of errors that have occured */
	Pop3State_t state; /**< current POP state */
	int was_apop;	/**< 1 if session was  session was apop (no plaintext password) */

	int SessionResult; /**< what happened during the session */

	char *username;
	char *password;

	char *apop_stamp; /**< timestamp for APOP */

	u64_t useridnr;	/**< Used by timsieved */

	u64_t totalsize;/**< total size of messages */
	u64_t virtual_totalsize;
	u64_t totalmessages; /**< number of messages */
	u64_t virtual_totalmessages;

	struct dm_list messagelst; /** list of messages */
} PopSession_t;



/*
 * define some IMAP symbols
 */
#define IMAP_NFLAGS 6

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
	IMAPCS_LOGOUT
};

enum IMAP4_FLAGS { 
	IMAPFLAG_SEEN		= 0x01, 
	IMAPFLAG_ANSWERED	= 0x02,
	IMAPFLAG_DELETED	= 0x04, 
	IMAPFLAG_FLAGGED	= 0x08,
	IMAPFLAG_DRAFT		= 0x10, 
	IMAPFLAG_RECENT		= 0x20
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
	BFIT_TEXT, 
	BFIT_HEADER, 
	BFIT_MIME,
	BFIT_HEADER_FIELDS,
	BFIT_HEADER_FIELDS_NOT, 
	BFIT_TEXT_SILENT
};

/* maximum size of a mailbox name */
#define IMAP_MAX_MAILBOX_NAMELEN 100

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


typedef struct {
	int no_daemonize;
	int log_verbose;
	char *pidFile;
	char *stateFile;
	int startChildren;
	int minSpareChildren;
	int maxSpareChildren;
	int maxChildren;
	int childMaxConnect;
	int timeout;
	int login_timeout;
	char **iplist; // Allocated memory.
	int ipcount;
	int *listenSockets; // Allocated memory.
	int service_before_smtp;
	int port;
	int backlog;
	int resolveIP;
	field_t serverUser, serverGroup;
	field_t socket;
	field_t log, error_log;
	field_t pid_dir;
	field_t state_dir;
	int (*ClientHandler) (clientinfo_t *);
} serverConfig_t;

/* 
 * (imap) mailbox data type
 */
typedef struct {
	u64_t uid, msguidnext, owner_idnr;
	char *name;
	unsigned no_select, no_inferiors, exists, recent, unseen, no_children, flags;
	int permission;
	time_t mtime;
	gboolean is_public, is_users, is_inbox;
} mailbox_t;


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

/**
 * remembering database positions for mail
 */
typedef struct {
	u64_t block, pos;
} db_pos_t;

/* 
 * simple cache mechanism
 */
typedef struct {
	struct DbmailMessage *dmsg;
	MEM *memdump, *tmpdump;
	u64_t num;
	int file_dumped, msg_parsed;
	u64_t dumpsize;
} cache_t;


/*
 * structure for basic message info 
 * so it can be retrieved at once
 */
typedef struct {
	int flags[IMAP_NFLAGS];
	char internaldate[IMAP_INTERNALDATE_LEN];
	u64_t rfcsize, uid;
} msginfo_t;

/*
 * A struct to hold info about a Sieve script
 */
typedef struct ssinfo {
	char *name;
	int active;
} sievescript_info_t;

/* messageblk types */
typedef enum {
	BODY_BLOCK = 0,
	HEAD_BLOCK = 1
} blocktype_t;

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
	SQL_REGEXP,
	SQL_SENSITIVE_LIKE,
	SQL_INSENSITIVE_LIKE,
	SQL_ENCODE_ESCAPE,
	SQL_STRCASE
		
} sql_fragment_t;

typedef enum {
	IMAP_FLAG_SEEN,
	IMAP_FLAG_ANSWERED,
	IMAP_FLAG_DELETED,
	IMAP_FLAG_FLAGGED,
	IMAP_FLAG_DRAFT,
	IMAP_FLAG_RECENT
} imap_flag_t;


#endif
