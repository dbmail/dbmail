# Don't forget to fire up Dbmail's IMAP server before starting this script:
#  sudo dbmail-imapd
# Run this file with:
#  ruby test-scripts/testextensions.rb
require "test/unit"
require "net/imap"
# load the time extensions (for Time.now.prev_month)
require "active_support/all"

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
  IMAP_COPYTO = 'Test'

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

  def test_copy
    imap_conn
    # take the first imap user
    user = IMAP_LOGINS[0]
    @imap.login(user[:username], user[:password])

    # check for regressions, see whether noop returns what it was returning before the extensions
    res = @imap.noop
    assert_equal("NOOP completed", res.data.text, 'NOOP regression test')
    # check what's the uid_next value and uidvalidity in the target folder
    @imap.select(IMAP_COPYTO)
    old_uid_validity = @imap.responses["UIDVALIDITY"][-1]
    old_uid_next = @imap.responses["UIDNEXT"][-1]

    # switch to the inbox folder
    @imap.select("inbox")
    # find messages that were sent in the last month
    # Set up the search keys
    search_keys = ['SENTSINCE', Time.now.prev_month.strftime("%e-%b-%Y")]
    # Search on imap, returns the uids
    uids = @imap.uid_search(search_keys)
    # Get the last 4 messages (or less)
    uids = uids.slice(uids.size > 4 ? uids.size - 4 : 0, 4) # do it this way, because slice returns nil for indexes out of range
    # assert that there are any uids, if this fails it's rather the test data's "bug" than the application's
    assert(!uids.empty?, 'There are uids')

    res = @imap.uid_copy(uids, IMAP_COPYTO)
    code = res.data.code

    assert_respond_to(code, 'name', 'Response code responds to name')
    assert_respond_to(code, 'data', 'Response code responds to data')
    # check the format of the answer
    assert_equal("COPYUID", code.name)
    uid_validity, orig_uids, new_uids = code.data.split(" ")
    assert_match(/^\d+$/, uid_validity)
    assert_match(/^\d+(:\d+)?(,\d+(:\d+)?)*$/, orig_uids) # based on the BNF in rfc4315
    assert_match(/^\d+(:\d+)?(,\d+(:\d+)?)*$/, new_uids) # based on the BNF in rfc4315
    orig_uids = @imap.format_imap_to_internal(orig_uids)
    new_uids = @imap.format_imap_to_internal(new_uids)

    assert_equal(old_uid_validity, uid_validity.to_i, 'Uidvalidity matches')
    assert_equal(uids.sort, orig_uids.sort, 'Old uids returned by UID COPY match the ones that we actually copied')
    assert_equal(orig_uids.size, new_uids.size, 'Old and new uids have the same number of elements')
    new_uids.each do |x|
      # check whether all new uids are above the last seen uid_next value
      assert(old_uid_next.to_i <= x.to_i, x.to_s+': the copied message\'s uid is higher than the last uid_next value ('+old_uid_next.to_s+')')
    end

    imap_logout
  end
  # test http://www.dbmail.org/mantis/view.php?id=978
  # create a hierarchy that's likely to reproduce the error
  def test_list
    imap_conn
    # take the first imap user
    user = IMAP_LOGINS[0]
    @imap.login(user[:username], user[:password])

    # check that the server is not dead after a simple list
    @imap.list("", "%")
    @imap.noop

    # create hierarchy
    base_name = "Test a"+0.upto(10).map{rand(10)}.join("")
    @imap.create(base_name)
    second_level = base_name+"/2012"
    @imap.create(second_level)
    subfolders = ['02', '04', '09', '03', '05', '06']
    subfolders.each do |x|
      @imap.create(second_level+"/"+x)
    end
    
    res = @imap.list("", "%")
    folder = res.find{|x| x.name == base_name}
    assert_not_equal(nil, folder, base_name+' is in the results')
    # check for the \Noselect error
    assert_equal(nil, folder.attr.find{|x| x == :Noselect}, 'noselect is not in attributes for '+base_name)

    res = @imap.list(base_name+"/", "%")
    folder = res.find{|x| x.name == second_level}
    assert_not_equal(nil, folder, second_level+' is in the results')
    # check for the \Noselect error
    assert_equal(nil, folder.attr.find{|x| x == :Noselect}, 'noselect is not in attributes for '+base_name)

    res = @imap.list(second_level+"/", "%")
    assert_equal(subfolders.size, res.size, 'all subfolders are returned')
    subfolders.each do |x|
      folder = res.find{|y| y.name == second_level+"/"+x}
      assert_not_equal(nil, folder, second_level+"/"+x+' is in the results')
    end

    subfolders.each do |x|
      @imap.delete(second_level+"/"+x)
    end
    @imap.delete(second_level)
    @imap.delete(base_name)
    
    imap_logout

    # Extra tasks:
    #
    # this will be nasty, check in the log, that all problematic memory was freed
    # you should enable this kind of debugging (from the C code), and clear the log, before running this!
    # Search for: reserving MailboxState_T pointer, freeing MailboxState_T pointer, reserving dynamic pointer, freeing dynamic pointer
    #ids = `grep 'reserving MailboxState_T pointer' /var/log/mail.log`.split("\n").map{|x| x.match(/(\d+)\]$/)[1]}.sort
    #freed_ids = `grep 'freeing MailboxState_T pointer' /var/log/mail.log`.split("\n").map{|x| x.match(/(\d+)\]$/)[1]}.sort
    #assert_equal([], ids - freed_ids)
    #assert_equal([], freed_ids - ids)
    #ids = `grep 'reserving dynamic pointer' /var/log/mail.log`.split("\n").map{|x| x.match(/(\d+)\]$/)[1]}.sort
    #freed_ids = `grep 'freeing dynamic pointer' /var/log/mail.log`.split("\n").map{|x| x.match(/(\d+)\]$/)[1]}.sort
    #assert_equal([], ids - freed_ids)
    #assert_equal([], freed_ids - ids)
  end

end