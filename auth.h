/*
 * auth.h
 *
 * generic header file for db authentication/user management functions
 * this can be implemented in any possible way 
 *
 * (c) 2000-2002 IC&S
 */

#ifndef _DBMAIL_AUTH_H
#define _DBMAIL_AUTH_H

#include "dbmailtypes.h"

/* #define _DBAUTH_STRICT_USER_CHECK */

int auth_connect();
int auth_disconnect();

u64_t auth_user_exists(const char *username);
int auth_get_known_users(struct list *users);
u64_t auth_getclientid(u64_t useridnr);
u64_t auth_getmaxmailsize(u64_t useridnr);
char* auth_getencryption(u64_t useridnr);

int auth_check_user (const char *username, struct list *userids, int checks);
int auth_check_user_ext(const char *username, struct list *userids, struct list *fwds, int checks);
u64_t auth_adduser (char *username, char *password, char *enctype, char *clientid, char *maxmail);
int auth_delete_user(const char *username);

int auth_change_username(u64_t useridnr, const char *newname);
int auth_change_password(u64_t useridnr, const char *newpass, const char *enctype);
int auth_change_clientid(u64_t useridnr, u64_t newcid);
int auth_change_mailboxsize(u64_t useridnr, u64_t newsize);

u64_t auth_validate (char *user, char *password);
u64_t auth_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp);

char *auth_get_userid (u64_t *useridnr);


#endif
