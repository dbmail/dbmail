
from zope.interface import Interface, Attribute
from zope import schema

class IMailFolder(Interface):
    """Interface used to define what methods the object
    provides, as well as which fields are available."""

    id = schema.TextLine(title=u"Object title")
    title = schema.TextLine(title=u"Object title")
    owner = schema.TextLine(title=u"Object title")

    def get_id():
        """Returns the instance id."""
                                
    def get_title():
        """Returns the instance title."""
                                
    def get_owner():
        """Returns the instance owner."""
                                

