Upgrading
=========

With this document committed into the repository, this is the plan for porting
from GMime 2.6 to GMime 3.0. Originally this list of instructions originates here:
https://github.com/GNOME/gmime/blob/master/PORTING

With every item checked from this list, the code changes and removal from this list
will be in the same commit.


Porting from GMime 2.6 to GMime 3.0
-----------------------------------

- g_mime_message_get_reply_to() no longer returns a const char*, instead it
  returns an InternetAddressList* for easier use.

- g_mime_message_set_sender() and g_mime_message_set_reply_to() have been
  removed. You will either need to use the appropriate getter method and
  then internet_address_list_add() to add a new InternetAddressMailbox
  or, alternatively, you can use g_mime_message_add_mailbox().

- GMimeRecipientType has been replaced by GMimeAddressType because it now
  contains non-recipient-based enum values (SENDER, FROM, and REPLY_TO).

- g_mime_message_get_recipients() has been replaced by g_mime_message_get_addresses()
  which allows you to access the address lists of any address header.

- g_mime_message_add_recipient() has been renamed to g_mime_message_add_mailbox()
  due to the fact that it can now be used to add mailbox addresses to the
  Sender, From, and Reply-To headers as well.

- g_mime_message_set_subject() now takes a charset argument used when encoding
  the subject into rfc2047 encoded-word tokens (if needed). Use `NULL` to
  get the old behavior of using a best-fit charset.

- Removed g_mime_message_[get,set]_date_as_string(). This is unnecessary since
  this can be done using g_mime_object_[get,set]_header().

- g_mime_set_user_charsets() and g_mime_user_charsets() have been removed.
  All encoding API's now have a way to specify a charset to use and all
  decoder API's take a GMimeParserOptions argument that allows for
  specifying fallback charsets.

- GMimeObject's prepend_header(), append_header(), set_header(), get_header(),
  and remove_header() virtual methods have all been removed. They have been
  replaced by the header_added(), header_changed(), header_removed(), and
  headers_cleared() virtual methods to allow users to set headers on the
  GMimeHeaderList directly and still get notifications of those changes.

- g_mime_object_new() and g_mime_object_new_with_type() both now take a
  GMimeParserOptions argument.

- g_mime_param_new_from_string() has been replaced by g_mime_param_list_parse()
  and now takes a GMimeParserOptions argument.

- g_mime_content_type_new_from_string() has been replaced by
  g_mime_content_type_parse() and now takes a GMimeParserOptions argument.

- g_mime_content_type_to_string() has been renamed to g_mime_content_type_get_mime_type().

- g_mime_content_type_get_params() has been renamed to g_mime_content_type_get_parameters().

- g_mime_content_disposition_new_from_string() has been replaced by
  g_mime_content_disposition_parse() and now takes a GMimeParserOptions argument.

- g_mime_content_disposition_to_string() has been replaced by g_mime_content_disposition_encode().

- g_mime_content_disposition_get_params() has been renamed to g_mime_content_disposition_get_parameters().

- internet_address_list_parse_string() has been replaced by
  internet_address_list_parse() and now takes a GMimeParserOptions argument.

- GMimeHeaderIter has been dropped in favour of a more direct way of
  iterating over a GMimeHeaderList using int indexes.

- g_mime_stream_write_to_stream(), g_mime_stream_writev(), and g_mime_stream_printf()
  now return a gint64.

- Renamed GMimeCertificateTrust to GMimeTrust. GMIME_CERTIFICATE_TRUST_NONE
  has been renamed to GMIME_TRUST_UNKNOWN and GMIME_CERTIFICATE_TRUST_FULLY has been 
  renamed to GMIME_TRUST_FULL.

- Removed g_mime_gpg_context_[get,set]_always_trust(). This can now be accomplished
  by passing GMIME_ENCRYPT_ALWAYS_TRUST to g_mime_crypto_context_encrypt().

- Removed g_mime_gpg_context_[get,set]_use_agent(). This should no longer be needed.

- Removed g_mime_gpg_context_[get,set]_auto_key_retrieve().

- When using GMimeGpgContext, there is no longer a way to explicitly
  set the path to the "gpg" executable.  Of course, users can still
  use the $PATH environment variable to select any executable named
  "gpg" as always.

- Removed g_mime_crypto_context_[get,set]_retrieve_session_key(). This is now handled by
  passing GMIME_DECRYPT_EXPORT_SESSION_KEY to the g_mime_crypto_context_decrypt()
  method.

- GMimeCryptoContext's encrypt, decrypt, and verify methods now all take a flags argument
  that can enable additional features (see above examples).

- g_mime_crypto_context_sign() now takes a boolean 'detach' argument that specifies whether
  or not to generate a detached signature. To get the old behavior, pass TRUE as the
  detach argument.

- g_mime_crypto_context_decrypt_session() has been merged with
  g_mime_crypto_context_decrypt() and so the decrypt method now takes a session_key
  argument that is allowed to be NULL.

- g_mime_crypto_context_verify() no longer takes a 'digest' argument.

- g_mime_multipart_signed_verify() and g_mime_multipart_encrypted_decrypt() no longer
  take GMimeCryptoContext arguments. Instead, they instantiate their own contexts
  based on the protocol specified in the Content-Type header. These methods now also
  take a flags argument and in the case of the decrypt() method, it now also takes a
  session_key argument.

- GMimeSignatureStatus and GMimeSignatureErrors have been merged into a
  single bitfield (GMimeSignatureStatus) which mirrors gpgme_sigsum_t.

- g_mime_stream_file_new_for_path() has been renamed to
  g_mime_stream_file_open() and now also takes a GError argument.

- g_mime_stream_fs_new_for_path() has been renamed to
  g_mime_stream_fs_open() and now also takes a GError argument.

- g_mime_part_new() now returns a GMimePart with a Content-Type of
  "application/octet-stream" instead of "text/plain" since there is
  now a GMimeTextPart who's g_mime_text_part_new() returns a
  GMimeTextPart with a Content-Type of "text/plain".

- g_mime_part_[get,set]_content_object() have been renamed to
  g_mime_part_[get,set]_content().

- g_mime_multipart_encrypted_encrypt() no longer takes a GMimeMultipartEncrypted
  argument nor does it return int. Instead, this function now returns a newly
  allocated GMimeMultipartEncrypted.

- g_mime_multipart_signed_sign() no longer takes a GMimeMultipartSigned
  argument nor does it return int. Instead, this function now returns a newly
  allocated GMimeMultipartSigned.

- g_mime_parser_[get,set]_scan_from() have been replaced by
  g_mime_parser_[get,set]_format() which takes a GMimeFormat argument.

- g_mime_parser_get_from() has been renamed to g_mime_parser_get_mbox_marker().

- g_mime_parser_get_from_offset() has been renamed to g_mime_parser_get_mbox_marker_offset().

- g_mime_parser_construct_[part,message]() now take a GMimeParserOptions argument.

- Renamed GMimeFilterMd5 to GMimeFilterChecksum.

- Renamed g_mime_multipart_[get,set]_preface() to g_mime_multipart_[get,set]_prologue().

- Renamed g_mime_multipart_[get,set]_postface() to g_mime_multipart_[get,set]_epilogue().

- Modified g_mime_object_write_to_stream() to take a GMimeFormatOptions argument.

- Split GMimeFilterCRLF into GMimeFilterUnix2Dos, GMimeFilterDos2Unix and GMimeFilterSmtpData.

