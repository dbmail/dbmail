Upgrading
=========

With this document committed into the repository, this is the plan for porting
from GMime 2.6 to GMime 3.0. Originally this list of instructions originates here:
https://github.com/GNOME/gmime/blob/master/PORTING

With every item checked from this list, the code changes and removal from this list
will be in the same commit.


Porting from GMime 2.6 to GMime 3.0
-----------------------------------

- g_mime_part_new() now returns a GMimePart with a Content-Type of
  "application/octet-stream" instead of "text/plain" since there is
  now a GMimeTextPart who's g_mime_text_part_new() returns a
  GMimeTextPart with a Content-Type of "text/plain".

- g_mime_part_[get,set]_content_object() have been renamed to
  g_mime_part_[get,set]_content().

- g_mime_parser_construct_[part,message]() now take a GMimeParserOptions argument.

- Renamed g_mime_multipart_[get,set]_preface() to g_mime_multipart_[get,set]_prologue().

- Renamed g_mime_multipart_[get,set]_postface() to g_mime_multipart_[get,set]_epilogue().
