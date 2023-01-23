/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2008 John T. Guthrie III guthrie@counterexample.org
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


/*
 * tls.c
 *
 * SSL/TLS-related routines.
 */

#include "dbmail.h"
#include <openssl/err.h>

#define THIS_MODULE "tls"


extern SSL_CTX *tls_context;

/* Create the initial SSL context structure */
SSL_CTX *tls_init(void) {
	SSL_CTX *ctx;
	SSL_library_init();
	SSL_load_error_strings();
	/* FIXME: We need to allow for the allowed SSL/TLS versions to be */
	/* configurable. */
	
	ctx = SSL_CTX_new(SSLv23_server_method());
	return ctx;
}

/* load the certificates into the context */
void tls_load_certs(ServerConfig_T *conf) 
{
	gboolean e = FALSE;
	/* load CA file */
	if (! (strlen(conf->tls_cafile) && strlen(conf->tls_cert) && strlen(conf->tls_key))) {
		conf->ssl = FALSE;
		return;
	}

	if (SSL_CTX_load_verify_locations(tls_context, conf->tls_cafile, NULL) == 0) {
		TRACE(TRACE_WARNING, "Error loading CA file [%s]: %s",
				conf->tls_cafile,
				tls_get_error());
		e = TRUE;
	}

	/* load certificate */
	if (SSL_CTX_use_certificate_file(tls_context, conf->tls_cert, SSL_FILETYPE_PEM) != 1) {
		TRACE(TRACE_WARNING, "Error loading certificate file [%s]: %s",
				conf->tls_cert,
				tls_get_error());
		e = TRUE;
	}

	/* load private key */
	if (SSL_CTX_use_PrivateKey_file(tls_context, conf->tls_key, SSL_FILETYPE_PEM) != 1) {
		TRACE(TRACE_WARNING, "Error loading key file [%s]: %s",
				conf->tls_key,
				tls_get_error());
		e = TRUE;
	}

	/* check certificate/private key consistency */
	if (SSL_CTX_check_private_key(tls_context) != 1) {
		TRACE(TRACE_WARNING, "Mismatch between certificate file [%s] and key file [%s]: %s",
				conf->tls_cert,
				conf->tls_key,
				tls_get_error());
		e = TRUE;
	}

	conf->ssl = e ? FALSE : TRUE;
}

/* load the ciphers into the context */
void tls_load_ciphers(ServerConfig_T *conf) {
	if (strlen(conf->tls_ciphers) &&
			SSL_CTX_set_cipher_list(tls_context, conf->tls_ciphers) == 0) {
		TRACE(TRACE_WARNING, "Unable to set any ciphers in list [%s]: %s",
				conf->tls_ciphers, tls_get_error());
	}
}

/* Grab the top error off of the error stack and then return a string
 * corresponding to that error */
char *tls_get_error(void) 
{
	return ERR_error_string(ERR_get_error(), NULL);
}

SSL *tls_setup(int fd) 
{
	SSL *ssl;

	if (! (ssl = SSL_new(tls_context))) {
		TRACE(TRACE_ERR, "Error creating TLS connection: %s", tls_get_error());
		return NULL;
	}
	UNBLOCK(fd);
	if ( !SSL_set_fd(ssl, fd)) {
		TRACE(TRACE_ERR, "Error linking SSL structure to file descriptor: %s", tls_get_error());
		SSL_shutdown(ssl);
		SSL_free(ssl);
		return NULL;
	}

	return ssl;
}

