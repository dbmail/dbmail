#ifndef DSN_H
#define DSN_H

#include <dbmailtypes.h>

/*
 * Struct to hold codes that correspond
 * to RFC 1893, and its successor, RFC 3463.
 * TODO: 
 * Plain text "translations" of these codes
 * are defined in the header dsn.h and
 * accessed by functions in dsn.c
 */

typedef enum
{
  DSN_CLASS_OK = 2,
  DSN_CLASS_TEMP = 4,
  DSN_CLASS_FAIL = 5
} dsn_class_t;

typedef struct
{
  dsn_class_t class;
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

/**
 * \brief Initialize a dsnuser structure and its lists.
 * \param dsnuser Pointer to a dsnuser structure in need of initialization.
 * \return
 *   - 0 on success
 *   - -1 on failure
 */
int dsnuser_init(deliver_to_user_t *dsnuser);

void dsnuser_free(deliver_to_user_t *dsnuser);
void dsnuser_free_list(struct list *deliveries);

/**
 * \brief Loop through the list of delivery addresses
 * and resolve them into lists of final delivery useridnr's,
 * and external forwarding addresses. Each dsnuser is flagged
 * with DSN codes so that successes and failures can be properly
 * indicated at the top of the delivery call chain, such as in
 * dbmail-smtp and dbmail-lmtpd.
 * \param deliveries Pointer to a list of dsnuser structs.
 * \return
 *   - 0 on success
 *   - -1 on failure
 */
int dsnuser_resolve_list(struct list *deliveries);

/**
 * \brief Loop through the list of delivery addresses
 * and find out what the single worst case scenario was
 * for situations where we are limited to returning a single
 * status code yet might have had a whole lot of deliveries.
 * \param deliveries Pointer to a list of dsnuser structs.
 * \return
 *   - see dsn_class_t for details.
 */
dsn_class_t dsnuser_worstcase_list(struct list *deliveries);

/**
 * \brief Given true/false values for each of the three
 * delivery classes, find out what the single worst case scenario was
 * for situations where we are limited to returning a single
 * status code yet might have had a whole lot of deliveries.
 * \param has_2 0 if nothing was DSN_CLASS_OK, 1 if there was.
 * \param has_4 0 if nothing was DSN_CLASS_TEMP, 1 if there was.
 * \param has_5 0 if nothing was DSN_CLASS_FAIL, 1 if there was.
 * \return
 *   - see dsn_class_t for details.
 */
dsn_class_t dsnuser_worstcase_int(int has_2, int has_4, int has_5);

#endif /* DSN_H */
