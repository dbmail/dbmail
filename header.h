/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _HEADER_H
#define _HEADER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "list.h"

/**
 * \brief Read from the specified FILE pointer until either
 * a long carriage-return line-feed or a lone period stand
 * on a line by themselves.
 * \param instream A FILE pointer to the stream where the header is.
 * \param headerrfcsize The size of the header if all lines ended in \r\n.
 * \param headersize The actual byte count of the header.
 * \param header A pointer to an unallocated char array. On
 * error, the pointer may not be valid and must not be used.
 * \return
 *      - 1 on success
 *      - 0 on failure
*/
int read_header(FILE *instream, u64_t *newlines, u64_t *headersize, char **header);

#endif
