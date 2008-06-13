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

ClientSession_t * client_session_new(client_sock *c);
int client_session_reset(ClientSession_t * session);
void client_session_reset_parser(ClientSession_t *session);
void client_session_bailout(ClientSession_t *session);
void client_session_set_timeout(ClientSession_t *session, int timeout);

void socket_read_cb(int fd, short what, void *arg);
void socket_write_cb(int fd, short what, void *arg);
 
int pop3_handle_connection(client_sock *c);
int imap_handle_connection(client_sock *c);
int tims_handle_connection(client_sock *c);
int lmtp_handle_connection(client_sock *c);
