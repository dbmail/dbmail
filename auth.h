/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
 * \file auth.h
 *
 * \brief generic header file for db authentication/user management functions
 * this can be implemented in any possible way 
 *
 * \author (c) 2000-2003 IC&S
 */

#ifndef _DBMAIL_AUTH_H
#define _DBMAIL_AUTH_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmailtypes.h"

#define MAX_CHECKS_DEPTH 1000
#define DBMAIL_USE_SAME_CONNECTION 0

/* #define _DBAUTH_STRICT_USER_CHECK */

/**
 * \brief connect to the authentication database. In case of an SQL connection,
 * no now connection is made (the already present database connection is
 * used). 
 * \return
 * 		- -1 on failure
 * 		-  0 on success
 */
int auth_connect(void);
/**
 * \brief disconnect from the authentication database. In case of an SQL
 * authentication connection, the connection is not released, because the
 * main dbmail database connection is used.
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_disconnect(void);

/**
 * \brief check if a user exists
 * \param username 
 * \param user_idnr will hold user_idnr after call. May not be NULL on call
 * \return 
 *    - -1 on database error
 *    -  0 if user not found
 *    -  1 otherwise
 */
int auth_user_exists(const char *username, /*@out@*/ u64_t * user_idnr);

/**
 * \brief get a list of all known users
 * \param users on call this should be an empty list. It will be filled with
 * all known users on return of this function, if return value is 0
 * \return
 *    - -2 on memory error
 *    - -1 on database error
 *    -  0 on success
 * \attention caller should free list
 */
int auth_get_known_users(struct list *users);

/**
 * \brief get client_id for a user
 * \param user_idnr
 * \param client_idnr will hold client_idnr after return. Must hold a valid
 * pointer on call
 * \return 
 *   - -1 on error
 *   -  1 on success
 */
int auth_getclientid(u64_t user_idnr, u64_t * client_idnr);

/**
 * \brief get the maximum mail size for a user
 * \param user_idnr
 * \param maxmail_size will hold value of maxmail_size after return. Must
 * hold a valid pointer on call.
 * \return
 *     - -1 if error
 *     -  0 if no maxmail_size found (which effectively is the same as a 
 *        maxmail_size of 0.
 *     -  1 otherwise
 */
int auth_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size);

/**
 * \brief returns a string describing the encryption used for the 
 * passwd storage for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 * If the specified user does not exist an empty string will be returned.
 * \param user_idnr
 * \return
 *    - NULL if error
 */
char *auth_getencryption(u64_t user_idnr);

/**
 * \brief find all deliver_to addresses for a username (?, code is not exactly
 * clear to me at the moment, IB 21-08-03)
 * \param username 
 * \param userids list of user ids (empty on call)
 * \param checks nr of checks. Used internally in recursive calls. It \b should
 * be set to -1 when called!
 * \return number of deliver_to addresses found
 */
int auth_check_user(const char *username, struct list *userids,
		    int checks);
/**
 * \brief as auth_check_user() but adds the numeric ID of the user found to
 * userids or the forward to the fwds list
 * \param username
 * \param userids list of user id's (empty on call)
 * \param fwds list of forwards (emoty on call)
 * \param checks used internally, \b should be -1 on call
 * \return number of deliver_to addresses found
 */
int auth_check_user_ext(const char *username, struct list *userids,
			struct list *fwds, int checks);
/**
 * \brief add a new user to the database (whichever type of database is 
 * implemented)
 * \param username name of new user
 * \param password his/her password
 * \param enctype encryption type of password
 * \param clientid client the user belongs with
 * \param maxmail maximum size of mailbox in bytes. (prepend with M for
 * megabytes or K for kilobytes)
 * \param user_idnr will hold the user_idnr of the user after return. Must hold
 * a valid pointer on call.
 * \return 
 *     - -1 on error
 *     -  1 on success
 * \bug this function creates its own query for adding a mailbox. It would probably
 * be a better idea to let db_create_mailbox() handle this.
 */
int auth_adduser(char *username, char *password, char *enctype,
		 char *clientid, char *maxmail, u64_t * user_idnr);
/**
 * \brief delete user from the database. Does not delete the user's email!
 * \param username name of user to be deleted
 * \return 
 *     - -1 on failure
 *     -  0 on success
 */
int auth_delete_user(const char *username);

/**
 * \brief change the username of a user.
 * \param user_idnr idnr identifying the user
 * \param new_name new name of user
 * \return
 *      - -1 on failure
 *      -  0 on success
 */
int auth_change_username(u64_t user_idnr, const char *new_name);
/**
 * \brief change a users password
 * \param user_idnr
 * \param new_pass new password (encrypted)
 * \param enctype encryption type of password
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_password(u64_t user_idnr,
			 const char *new_pass, const char *enctype);
/**
 * \brief change a users client id
 * \param user_idnr
 * \param new_cid new client id
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_clientid(u64_t user_idnr, u64_t new_cid);
/**
 * \brief change a user's mailbox size (maxmailsize)
 * \param user_idnr
 * \param new_size new size of mailbox
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size);
/**
 * \brief try to validate a user (used for login to server). 
 * \param username 
 * \param password
 * \param user_idnr will hold the user_idnr after return. Must be a pointer
 * to a valid u64_t variable on call.
 * \return
 *     - -1 on error
 *     -  0 if not validated
 *     -  1 if OK
 */
int auth_validate(char *username, char *password, u64_t * user_idnr);

/** 
 * \brief try tp validate a user using md5 hash
 * \param username
 * \param md5_apop_he md5 string
 * \param apop_stamp timestamp
 * \return 
 *      - -1 on error
 *      -  0 if not validated
 *      -  user_idrn if OK
 */
u64_t auth_md5_validate(char *username, unsigned char *md5_apop_he,
			char *apop_stamp);

/**
 * \brief get username for a user_idnr
 * \param user_idnr
 * \return
 *    - NULL on error
 *    - username otherwise
 * \attention caller should free username string
 */
char *auth_get_userid(u64_t user_idnr);


#endif
