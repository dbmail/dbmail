#ifndef DSN_H
#define DSN_H

#include <dbmailtypes.h>

/*
 * Struct to hold codes that correspond
 * to RFC 1893 its successor, RFC 3463.
 *
 * Plain text "translations" of these codes
 * are defined in the header dsn.h and
 * accessed by functions in dsn.c
 */
typedef struct
{
  int class;
  int subject;
  int detail;
} delivery_status_t;

typedef struct
{
  u64_t useridnr; /* Specific user id recipient (from outside). */
  const char *address; /* Envelope recipient (from outside). */
  const char *mailbox; /* Default mailbox to use for userid deliveries (from outside). */
  struct list *userids; /* List of u64_t* -- internal useridnr's to deliver to (internal). */
  struct list *forwards; /* List of char* -- external addresses to forward to (internal). */
  delivery_status_t dsn; /* Return status of this "delivery basket" (to outside). */
} deliver_to_user_t;

void dsnuser_init(deliver_to_user_t *dsnuser);
void dsnuser_free(deliver_to_user_t *dsnuser);

#endif /* DSN_H */
