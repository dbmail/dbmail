/*
  
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

#ifndef MAILBOXSTATE_H
#define MAILBOXSTATE_H

#include "dbmail.h"

#define T MailboxState_T

typedef struct T *T;

extern T            MailboxState_new(u64_t id);

extern int          MailboxState_preload(T);
extern void         MailboxState_remap(T);
extern int          MailboxState_removeUid(T, u64_t);
extern GTree *      MailboxState_getMsginfo(T);
extern void         MailboxState_setMsginfo(T, GTree *);
extern void         MailboxState_addMsginfo(T, u64_t, MessageInfo *);
extern GTree *      MailboxState_getIds(T);
extern GTree *      MailboxState_getMsn(T);


extern void         MailboxState_setId(T, u64_t);
extern u64_t        MailboxState_getId(T);
extern u64_t        MailboxState_getSeq(T);
extern u64_t        MailboxState_getUidnext(T);
extern unsigned	    MailboxState_getExists(T);
extern void         MailboxState_setRecent(T, u64_t);
extern unsigned	    MailboxState_getRecent(T);
extern unsigned     MailboxState_getUnseen(T);
extern void         MailboxState_setNoSelect(T, gboolean);
extern gboolean     MailboxState_noSelect(T);
extern void         MailboxState_setNoChildren(T, gboolean);
extern gboolean     MailboxState_noChildren(T);
extern gboolean     MailboxState_noInferiors(T);

extern void         MailboxState_setOwner(T S, u64_t owner_id);
extern u64_t        MailboxState_getOwner(T S);
extern void         MailboxState_setPermission(T S, int permission);
extern unsigned     MailboxState_getPermission(T S);
extern void         MailboxState_setName(T S, const char *name);
extern const char * MailboxState_getName(T S);

extern gboolean     MailboxState_isSubscribed(T);
extern void         MailboxState_setIsUsers(T, gboolean);
extern gboolean     MailboxState_isUsers(T);
extern void         MailboxState_setIsPublic(T, gboolean);
extern gboolean     MailboxState_isPublic(T);

extern gboolean     MailboxState_hasKeyword(T, const char *);
extern void         MailboxState_addKeyword(T, const char *);
	
extern char *       MailboxState_flags(T);

extern void         MailboxState_free(T *);

/**
 * \brief check if a user has a certain right to a mailbox
 */
extern int db_acl_has_right(T, u64_t user_idnr, const char *right_flag);
/**
 * \brief get all permissions on a mailbox for a user
 * 
 */
extern int db_acl_get_acl_map(T, u64_t userid, struct ACLMap *map);


#undef T

#endif
