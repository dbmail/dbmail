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

/* $Id$
 *
 * Header.c implements functions to read an email header
 * and parse out certain goodies, such as deliver-to
 * fields and common fields for the fast header cache
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "list.h"
#include "auth.h"
#include "mime.h"
#include "header.h"
#include "db.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HEADER_BLOCK_SIZE 1024

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

/**
 * get the rfc size of the body of a message. This function assumes that
 * the message has lines that end in '\n', not in '\r\n'
 * \param[in] body message body
 * \param body_size size of body
 * \param[out] rfcsize rfc size of body
 */
static void get_rfc_size(const char *body_start, u64_t body_size, 
			 u64_t *rfcsize);

/**
 * chew the next line of input
 * \param[in] message_content part of a message
 * \param[out] line_size size of line (including '\r\n' or '\n')
 * \param[out] line_rfcsize size of line (assuming '\r\n')
 * \return 
 *     - -1 on error
 *     - 0 if empty line (only \r\n or \n)
 *     - 1 if line found
 */
static int consume_header_line(const char *message_content,
			       size_t *line_size,
			       size_t *line_rfcsize);


int split_message(const char *whole_message, 
		  u64_t whole_message_size,
		  char **header, u64_t *header_size,
		  u64_t *header_rfcsize,
		  const char **body, u64_t *body_size,
		  u64_t *body_rfcsize)
{
	const char *end_of_header;
	size_t line_size;
	size_t line_rfcsize;
	size_t body_begin;
	u64_t tmp_header_size = 0;
	u64_t tmp_header_rfcsize = 0;
	int result;
	
	end_of_header = whole_message;
	
	/* split off the header */
	while (1) {
		result = consume_header_line(end_of_header,
					     &line_size, &line_rfcsize);
		if (result < 0) 
			return -1;

		tmp_header_size += line_size;
		tmp_header_rfcsize += line_rfcsize;
		end_of_header = &whole_message[tmp_header_size];
		
		if (result == 0) {
			break;
		}
	}
	
	/* copy the header */
	*header = my_malloc((tmp_header_size + 1) * sizeof(char));
	if (header == NULL) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory", 
		      __FILE__, __func__);
		return -1;
	}
	memset(*header, '\0', tmp_header_size + 1);
	strncpy(*header, whole_message, tmp_header_size);

	*header_size = tmp_header_size;
	*header_rfcsize = tmp_header_rfcsize;

	/* MTAs seem to add another newline after the empty header line.
	 * that empty line needs to be skipped. So, if the next character
	 */
	body_begin = tmp_header_size;
	if (whole_message_size - tmp_header_size >= 2) {
		if (whole_message[body_begin] == '\n')
			body_begin += 1;
		else if (whole_message[body_begin] == '\r' &&
			 whole_message[body_begin + 1] == '\n')
			body_begin += 2;
	}	
	*body = &whole_message[body_begin];
	*body_size = whole_message_size - body_begin;

	get_rfc_size(*body, *body_size, body_rfcsize);

	return 1;
}

void get_rfc_size(const char *body_start, u64_t body_size, 
		  u64_t *rfcsize)
{
	const char *current_pos;
	size_t remaining_len;
	unsigned int rfcadd = 0;
	
	current_pos = body_start;
	remaining_len = body_size;

	trace(TRACE_DEBUG, "%s,%s: remaining_len = %zd", 
	      __FILE__, __func__, remaining_len);
	if (remaining_len == 0)
		return;
	/* count all the newlines */
	while((current_pos = memchr(current_pos, '\n', remaining_len))) {
		rfcadd++;
		remaining_len = body_size - (current_pos - body_start) - 1;
		if (remaining_len > 0)
			current_pos++;
	}
		
	*rfcsize = body_size + rfcadd;
}

static int consume_header_line(const char *message_content,
			    size_t *line_size,
			    size_t *line_rfcsize)
{
	size_t line_content_size;
	size_t tmp_line_size = 0;
	size_t tmp_line_rfcsize = 0;
	
	if (strlen(message_content) == 0) {
		tmp_line_size = 0;
		tmp_line_rfcsize = 0;
	} else {
		line_content_size = strcspn(message_content, "\r\n");
		if (message_content[line_content_size] == '\n') {
			tmp_line_size = line_content_size + 1;
			tmp_line_rfcsize = tmp_line_size + 1;
		} else {
			if (message_content[line_content_size] == '\r' &&
			    message_content[line_content_size + 1] == '\n') {
				tmp_line_size = line_content_size + 2;
				tmp_line_rfcsize = tmp_line_size;
			} else {
				trace(TRACE_ERROR, "%s,%s: error reading header line",
				      __FILE__, __func__);
				return -1;
			}
		}
	}
	*line_size = tmp_line_size;
	*line_rfcsize = tmp_line_rfcsize;

	if (tmp_line_rfcsize == 2 || tmp_line_rfcsize == 0) {
		/* only '\r\n', which is an empty line */
		trace(TRACE_DEBUG, "%s,%s: end of header found",
		      __FILE__, __func__);
		return 0;
	}
	else
		return 1;
	
}
