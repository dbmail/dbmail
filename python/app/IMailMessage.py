
from zope.interface import Interface, Attribute
from zope import schema

class IMailMessage(Interface):
    """Interface used to define what methods the object
    provides, as well as which fields are available."""

    id = schema.TextLine(title=u"Message id")
    folder = schema.TextLine(title=u"Message container")

    def get_id():
        """Returns the message id."""
                                
    def get_folder():
        """Returns the folder containing this message."""
                                
    def get_content():
        """Return the full message"""

    def set_content():
        """Store the full message"""

    def get_header():
        """Return a header"""

    def del_header():
        """Delete a header"""

    def add_header():
        """Add a header"""

