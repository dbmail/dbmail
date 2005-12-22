/* Dynamic loading of the database backend.
 * We use GLib's multiplatform dl() wrapper
 * to open up sort_sieve.so and
 * populate the global 'sort' structure.
 *
 * (c) 2005 Aaron Stone <aaron@serendipity.cx>
 */

#ifndef SORTMODULE_H
#define SORTMODULE_H

/* Prototypes must match with those in sort.h
 * and in the sorting drivers. */
typedef struct {
	int (* connect)(void);
	int (* disconnect)(void);
	int (* validate)(u64_t user_idnr, char *scriptname, char **errmsg);
	int (* process)(u64_t user_idnr, struct DbmailMessage *message,
		const char *fromaddr);
} sort_func_t;


#endif
