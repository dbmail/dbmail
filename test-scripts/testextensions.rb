# Don't forget to fire up Dbmail's IMAP server before starting this script:
#  sudo dbmail-imapd
# Run this file with:
#  ruby test-scripts/testextensions.rb
require "test/unit"
require "net/imap"

class MyIMAP < Net::IMAP
  #@@debug = true

  # we need to do the exact reverse of format_internal()
  #  - we need to handle cases in the reverse order
  #  - we have to execute the reverse operations
  def format_imap_to_internal(data)
    if data.include?(',')
      return data.split(",").map{|x| format_imap_to_internal(x)}
    elsif data =~ /^(\d+):(\d+)$/
      return format_imap_to_internal($1)..format_imap_to_internal($2)
    elsif data =~ /^\d+$/
      return data.to_i
    elsif data == "*"
      return data
    end
  end

  def uid_copy_with_new_uid(uid, target)
    res = self.uid_copy(uid, target)
    code = res.data.code
    if code.name == 'COPYUID'
      new_uid = code.data.split(" ")[2]
      if new_uid
        new_uid = format_imap_to_internal(new_uid)
        return new_uid
      end
    end
    return nil
  end

  def uid_delete_without_expunge(uid, trash)
    new_uid = self.uid_copy_with_new_uid(uid, trash)
    self.uid_store(uid, "+FLAGS", [:Deleted])
    self.close
    self.select(trash)
    self.uid_store(new_uid, "+FLAGS", [:Deleted])
    self.close
  end
end

class TestImap < Test::Unit::TestCase
  # Set up some configuration variables
  IMAP_SERVER = 'localhost'
  IMAP_PORT = 143
  # Set up Dbmail's settings to use LDAP.
  # And then add the login credentials that you would like to test here.
  IMAP_LOGINS = [
    {:username => 'username', :password => 'password'},
    {:username => 'username', :password => 'password'}
  ]

  def imap_conn
    Net::IMAP.debug = true
    @imap = MyIMAP.new(IMAP_SERVER, IMAP_PORT) # without ssl
  end

  def imap_logout
    @imap.logout
  rescue Net::IMAP::ResponseParseError # catch \r\n after BYE responses (TODO: is it a failure of Net::IMAP or Dbmail??)
    # do nothing
  end

  def test_append
    imap_conn
    # take the first imap user
    user = IMAP_LOGINS[0]
    @imap.login(user[:username], user[:password])
    # check for regressions, see whether noop returns what it was returning before the extensions
    res = @imap.noop
    assert_equal("NOOP completed", res.data.text, 'NOOP regression test')
    # check what's the uid_next value and uidvalidity
    @imap.select("inbox")
    old_uid_validity = @imap.responses["UIDVALIDITY"][-1]
    old_uid_next = @imap.responses["UIDNEXT"][-1]
    # save a new message
    res = @imap.append("inbox", <<EOF.gsub(/\n/, "\r\n"), [:Seen], Time.now)
Subject: hello
From: test@moveoneinc.com
To: test@moveoneinc.com

hello world
EOF
    code = res.data.code
    # check the format of the answer
    assert_equal("APPENDUID", code.name)
    uid_validity, uid_set = code.data.split(" ")
    assert_match(/^\d+$/, uid_validity)
    assert_match(/^\d+$/, uid_set)
    #The next line would be necessary in case of multiappend
    #assert_match(/^\d+(:\d+)?(,\d+(:\d+)?)*$/, uid_set) # based on the BNF in rfc4315
    # check whether the uid_validity and uid_next match
    assert_equal(old_uid_validity, uid_validity.to_i, 'Uidvalidity matches')
    assert(old_uid_next.to_i <= uid_set.to_i, 'The appended message\'s uid is higher than the last uid_next value')
    imap_logout
  end
end