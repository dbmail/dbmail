Upgrading
=========

With this document committed into the repository, this is the plan for porting
from GMime 2.6 to GMime 3.0. Originally this list of instructions originates here:
https://github.com/GNOME/gmime/blob/master/PORTING

With every item checked from this list, the code changes and removal from this list
will be in the same commit.


Additions to Porting from GMime 2.6 to GMime 3.0
------------------------------------------------

Well... apparently, the porting guide from gmime is not very complete. I will add my
verbose documentation on the changes I made, and will probably shared them with the
gmime project when I'm finished.

- Modified g_mime_object_to_string() to take a GMimeFormatOptions argument.

- Modified g_mime_object_get_header() to take a GMimeFormatOptions argument.

- g_mime_header_list_set_stream() is removed

- g_mime_utils_header_decode_date() is now based on GDateTime.

- g_mime_object_prepend_header() and g_mime_object_append_header() now require a charset
  parameter that is nullable. It is not documented to be nullable, but following the 
  charset variable getting passed around, it is finally used in
  g_mime_utils_header_decode_text(), where it can be null.

- g_mime_utils_header_encode_text() has two new NULLable parameters: options and charset
