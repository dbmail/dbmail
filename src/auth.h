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
 * \file auth.h
 *
 * \brief generic header file for db authentication/user management functions
 * this can be implemented in any possible way 
 *
 * \author (c) 2000-2003 IC&S
 */

#ifndef DM_AUTH_H
#define DM_AUTH_H

#include "dbmail.h"

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
 *    -  0 if user not found
 *    -  1 otherwise
 */
int auth_user_exists(const char *username, /*@out@*/ uint64_t * user_idnr);

/**
 * \brief get username for a user_idnr
 * \param user_idnr
 * \return
 *    - NULL on error
 *    - username otherwise
 * \attention caller should free username string
 */
char *auth_get_userid(uint64_t user_idnr);

/**
 * \brief checks if a user_idnr exists
 * \param user_idnr
 * \return
 *    - 0 if user exists
 *    - 1 if user doesn't exist
 *    - -1 if something went wrong
 */
int auth_check_userid(uint64_t user_idnr);


/**
 * \brief get a list of all known users
 * \return
 *    - list off all usernames on success
 *    - NULL on error
 * \attention caller should free list
 */
GList * auth_get_known_users(void);

/**
 * \brief get a list of all known aliases
 * \return
 *    - list off all forwards on success
 *    - NULL on error
 * \attention caller should free list
 */
GList * auth_get_known_aliases(void);

/**
 * \brief get client_id for a user
 * \param user_idnr
 * \param client_idnr will hold client_idnr after return. Must hold a valid
 * pointer on call
 * \return 
 *   - -1 on error
 *   -  1 on success
 */
int auth_getclientid(uint64_t user_idnr, uint64_t * client_idnr);

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
int auth_getmaxmailsize(uint64_t user_idnr, uint64_t * maxmail_size);

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
char *auth_getencryption(uint64_t user_idnr);

/**
 * \brief as auth_check_user() but adds the numeric ID of the user found to
 * userids or the forward to the fwds list
 * \param username
 * \param userids list of user id's (empty on call)
 * \param fwds list of forwards (emoty on call)
 * \param checks used internally, \b should be -1 on call
 * \return number of deliver_to addresses found
 */
int auth_check_user_ext(const char *username, GList **userids, GList **fwds, int checks);
/**
 * \brief add a new user to the database (whichever type of database is 
 * implemented)
 * \param username name of new user
 * \param password his/her password
 * \param enctype encryption type of password
 * \param clientid client the user belongs with
 * \param maxmail maximum size of mailbox in bytes
 * \param user_idnr will hold the user_idnr of the user after return. Must hold
 * a valid pointer on call.
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
int auth_adduser(const char *username, const char *password, const char *enctype,
		 uint64_t clientid, uint64_t maxmail, uint64_t * user_idnr);
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
int auth_change_username(uint64_t user_idnr, const char *new_name);
/**
 * \brief change a users password
 * \param user_idnr
 * \param new_pass new password (encrypted)
 * \param enctype encryption type of password
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_password(uint64_t user_idnr,
			 const char *new_pass, const char *enctype);
/**
 * \brief change a users client id
 * \param user_idnr
 * \param new_cid new client id
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_clientid(uint64_t user_idnr, uint64_t new_cid);
/**
 * \brief change a user's mailbox size (maxmailsize)
 * \param user_idnr
 * \param new_size new size of mailbox
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
int auth_change_mailboxsize(uint64_t user_idnr, uint64_t new_size);
/**
 * \brief try to validate a user (used for login to server). 
 * \param username 
 * \param password
 * \param user_idnr will hold the user_idnr after return. Must be a pointer
 * to a valid uint64_t variable on call.
 * \return
 *     - -1 on error
 *     -  0 if not validated
 *     -  1 if OK
 */
int auth_validate(ClientBase_T *ci, const char *username, const char *password, uint64_t * user_idnr);

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
uint64_t auth_md5_validate(ClientBase_T *ci, char *username, unsigned char *md5_apop_he,
			char *apop_stamp);

/**
 * \brief get deliver_to from alias. Gets a list of deliver_to
 * addresses
 * \param alias the alias
 * \return 
 *         - NULL on failure
 *         - "" if no such alias found
 *         - deliver_to address otherwise
 * \attention caller needs to free the return value
 */
char *auth_get_deliver_from_alias(const char *alias);
/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 */
GList * auth_get_user_aliases(uint64_t user_idnr);
/**
 * \brief get a list of forwards associated with an external alias
 * \param alias the alias
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 */
GList * auth_get_aliases_ext(const char *alias);
/**
 * \brief add an alias for a user
 * \param user_idnr user's id
 * \param alias new alias
 * \param clientid client id
 * \return 
 *        - -1 on failure
 *        -  0 on success
 *        -  1 if alias already exists for given user
 */
int auth_addalias(uint64_t user_idnr, const char *alias, uint64_t clientid);
/**
 * \brief add an alias to deliver to an extern address
 * \param alias the alias
 * \param deliver_to extern address to deliver to
 * \param clientid client idnr
 * \return 
 *        - -1 on failure
 *        - 0 on success
 *        - 1 if deliver_to already exists for given alias
 */
int auth_addalias_ext(const char *alias, const char *deliver_to,
		    uint64_t clientid);
/**
 * \brief remove alias for user
 * \param user_idnr user id
 * \param alias the alias
 * \return
 *         - -1 on failure
 *         - 0 on success
 */
int auth_removealias(uint64_t user_idnr, const char *alias);
/**
 * \brief remove external delivery address for an alias
 * \param alias the alias
 * \param deliver_to the deliver to address the alias is
 *        pointing to now
 * \return
 *        - -1 on failure
 *        - 0 on success
 */
int auth_removealias_ext(const char *alias, const char *deliver_to);
/**
 * \brief determines if the loaded auth module requires shadow users
 * \return
 *         - TRUE if required
 *         - FALSE if not
 */
gboolean auth_requires_shadow_user(void);

#endif
