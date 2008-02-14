/*
 Copyright (C) 2004 IC & S dbmail@ic-s.nl
 Copyright (C) 2008 NFG Net Facilities Group BV, support@nfg.nl

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

ClientSession_t * client_session_new(clientinfo_t *ci);
int client_session_reset(ClientSession_t * session);
void client_session_reset_parser(ClientSession_t *session);
void client_session_bailout(ClientSession_t *session);

void socket_read_cb(struct bufferevent *ev, void *arg);
void socket_write_cb(struct bufferevent *ev, void *arg);
void socket_error_cb(struct bufferevent *ev, short what, void *arg);
 
int pop3_handle_connection(clientinfo_t *ci);
int imap_handle_connection(clientinfo_t *ci);
int tims_handle_connection(clientinfo_t *ci);
int lmtp_handle_connection(clientinfo_t *ci);
