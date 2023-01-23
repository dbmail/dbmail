/*
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

#ifndef DM_MAILBOXSTATE_H
#define DM_MAILBOXSTATE_H

#include "dbmail.h"

#define T MailboxState_T


struct MailboxState_T {
	Mempool_T pool;
	gboolean freepool;
	uint64_t id;
	uint64_t uidnext;
	uint64_t owner_id;
	uint64_t seq;
	// the seq of the mailbox when the state is loaded
	uint64_t state_seq;
	// when loading is in differential mode, this flag will be incremented
	unsigned differential_iterations;
	//
	unsigned no_select;
	unsigned no_children;
	unsigned no_inferiors;
	unsigned recent;
	unsigned exists;
	unsigned unseen;
	unsigned permission;
	// 
	gboolean is_subscribed;
	gboolean is_public;
	gboolean is_users;
	gboolean is_inbox;
	//
	String_T name;
	GTree *keywords;
	GTree *msginfo;
	GTree *ids;
	GTree *msn;
	GTree *recent_queue;
};

typedef struct T *T;

extern T            MailboxState_new(Mempool_T pool, uint64_t id);
extern T			MailboxState_update(Mempool_T pool, T OldM);

extern int          MailboxState_info(T);
extern int          MailboxState_count(T);
extern void         MailboxState_remap(T);
extern int          MailboxState_build_recent(T);
extern int          MailboxState_flush_recent(T);
extern int          MailboxState_clear_recent(T);
extern int          MailboxState_merge_recent(T, T);

extern int          MailboxState_removeUid(T, uint64_t);
extern void         MailboxState_addMsginfo(T, uint64_t, MessageInfo *);
extern GTree *      MailboxState_getMsginfo(T);
extern GTree *      MailboxState_getIds(T);
extern GTree *      MailboxState_getMsn(T);


extern void         MailboxState_setId(T, uint64_t);
extern uint64_t     MailboxState_getId(T);
extern uint64_t     MailboxState_getSeq(T);
extern void         MailboxState_resetSeq(T);
extern uint64_t     MailboxState_resyncSeq(T);

extern uint64_t     MailboxState_getUidnext(T);
extern unsigned	    MailboxState_getExists(T);
extern void	    MailboxState_setExists(T, unsigned);
extern unsigned	    MailboxState_getRecent(T);
extern unsigned     MailboxState_getUnseen(T);
extern void         MailboxState_setNoSelect(T, gboolean);
extern gboolean     MailboxState_noSelect(T);
extern void         MailboxState_setNoChildren(T, gboolean);
extern gboolean     MailboxState_noChildren(T);
extern gboolean     MailboxState_noInferiors(T);

extern void         MailboxState_setOwner(T S, uint64_t owner_id);
extern uint64_t     MailboxState_getOwner(T S);
extern void         MailboxState_setPermission(T S, int permission);
extern unsigned     MailboxState_getPermission(T S);
extern void         MailboxState_setName(T S, const char *name);
extern const char * MailboxState_getName(T S);

extern void         MailboxState_setIsUsers(T, gboolean);
extern gboolean     MailboxState_isUsers(T);
extern void         MailboxState_setIsPublic(T, gboolean);
extern gboolean     MailboxState_isPublic(T);

extern gboolean     MailboxState_hasKeyword(T, const char *);
extern void         MailboxState_addKeyword(T, const char *);
	
extern char *       MailboxState_flags(T);
extern GList *      MailboxState_message_flags(T, MessageInfo *);
extern GTree *      MailboxState_get_set(T, const char *, gboolean);

extern void         MailboxState_free(T *);

/**
 * \brief check if a user has a certain right to a mailbox
 */
extern int MailboxState_hasPermission(T, uint64_t user_idnr, const char *right_flag);
/**
 * \brief get all permissions on a mailbox for a user
 * 
 */
extern int MailboxState_getAcl(T, uint64_t userid, struct ACLMap *map);


#undef T

#endif
