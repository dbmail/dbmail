/*
 * dbauth.h
 *
 * generic header file for db authentication/user management functions
 */

#ifndef _DBAUTH_H
#define _DBAUTH_H

#include "dbmailtypes.h"

u64_t db_user_exists(const char *username);
int db_get_known_users(struct list *users);
u64_t db_getclientid(u64_t useridnr);
u64_t db_getmaxmailsize(u64_t useridnr);


int db_check_user (char *username, struct list *userids, int checks);
u64_t db_adduser (char *username, char *password, char *clientid, char *maxmail);
int db_delete_user(const char *username);

int db_change_username(u64_t useridnr, const char *newname);
int db_change_password(u64_t useridnr, const char *newpass);
int db_change_clientid(u64_t useridnr, u64_t newcid);
int db_change_mailboxsize(u64_t useridnr, u64_t newsize);

u64_t db_validate (char *user, char *password);
u64_t db_md5_validate (char *username,unsigned char *md5_apop_he, char *apop_stamp);

char *db_get_userid (u64_t *useridnr);


#endif
