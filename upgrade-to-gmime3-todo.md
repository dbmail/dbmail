Upgrading
=========

With this document committed into the repository, this is the plan for porting
from GMime 2.6 to GMime 3.0. Originally this list of instructions originates here:
https://github.com/GNOME/gmime/blob/master/PORTING

With every item checked from this list, the code changes and removal from this list
will be in the same commit.


Porting from GMime 2.6 to GMime 3.0
-----------------------------------

- g_mime_message_set_sender() and g_mime_message_set_reply_to() have been
  removed. You will either need to use the appropriate getter method and
  then internet_address_list_add() to add a new InternetAddressMailbox
  or, alternatively, you can use g_mime_message_add_mailbox().

- g_mime_message_add_recipient() has been renamed to g_mime_message_add_mailbox()
  due to the fact that it can now be used to add mailbox addresses to the
  Sender, From, and Reply-To headers as well.

- g_mime_message_set_subject() now takes a charset argument used when encoding
  the subject into rfc2047 encoded-word tokens (if needed). Use `NULL` to
  get the old behavior of using a best-fit charset.

- Removed g_mime_message_[get,set]_date_as_string(). This is unnecessary since
  this can be done using g_mime_object_[get,set]_header().

- GMimeObject's prepend_header(), append_header(), set_header(), get_header(),
  and remove_header() virtual methods have all been removed. They have been
  replaced by the header_added(), header_changed(), header_removed(), and
  headers_cleared() virtual methods to allow users to set headers on the
  GMimeHeaderList directly and still get notifications of those changes.

- g_mime_object_new() and g_mime_object_new_with_type() both now take a
  GMimeParserOptions argument.

- g_mime_content_type_to_string() has been renamed to g_mime_content_type_get_mime_type().

- g_mime_content_disposition_new_from_string() has been replaced by
  g_mime_content_disposition_parse() and now takes a GMimeParserOptions argument.

- g_mime_content_disposition_to_string() has been replaced by g_mime_content_disposition_encode().

- internet_address_list_parse_string() has been replaced by
  internet_address_list_parse() and now takes a GMimeParserOptions argument.

- g_mime_part_new() now returns a GMimePart with a Content-Type of
  "application/octet-stream" instead of "text/plain" since there is
  now a GMimeTextPart who's g_mime_text_part_new() returns a
  GMimeTextPart with a Content-Type of "text/plain".

- g_mime_part_[get,set]_content_object() have been renamed to
  g_mime_part_[get,set]_content().

- g_mime_parser_construct_[part,message]() now take a GMimeParserOptions argument.

- Renamed g_mime_multipart_[get,set]_preface() to g_mime_multipart_[get,set]_prologue().

- Renamed g_mime_multipart_[get,set]_postface() to g_mime_multipart_[get,set]_epilogue().
