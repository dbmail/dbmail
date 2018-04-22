Upgrading
=========

With this document committed into the repository, this is the plan for porting
from GMime 2.6 to GMime 3.0. Originally this list of instructions originates here:
https://github.com/GNOME/gmime/blob/master/PORTING

With every item checked from this list, the code changes and removal from this list
will be in the same commit.


Porting from GMime 2.6 to GMime 3.0
-----------------------------------

- Renamed g_mime_multipart_[get,set]_preface() to g_mime_multipart_[get,set]_prologue().

- Renamed g_mime_multipart_[get,set]_postface() to g_mime_multipart_[get,set]_epilogue().
