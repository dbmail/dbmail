<?php

/* to run this:

%> phpunit testdbmailclient.php

*/

#require_once "PHPUnit/Framework.php";
require_once "dbmailclient.php";

class testdbmailclient extends PHPUnit_Framework_TestCase 
{
	public function setUp()
	{
		$this->dm = new DBMail($host="localhost", $port=41380, $login="admin",$password="secret");
		$this->user = $this->dm->getUser('testuser1');
		$this->mbox = $this->user->getMailbox('INBOX');
		$this->user->addMailbox("somenewmailbox2");
	}

	public function tearDown()
	{
		$this->user->delete("somenewmailbox1");
		$this->user->delete("somenewmailbox2");
	}

	public function testDBMail()
	{
		$this->assertNotNull($this->dm);
	}

	public function testjson()
	{
		$j = '{"users": {
			"123": { "name": "testuser2" },
			"10005": { "name": "testuser1" },
			"10012": { "name": "__public__" },
			"10013": { "name": "anyone" },
			"20007": { "name": "testuser3" },
			"10016": { "name": "tst_source" },
			"10008": { "name": "tst_target" },
			"20002": { "name": "test@test" }
			}}';
		$this->assertNotNull(json_decode('{"users": {"1": {"name": "test" }}}', TRUE));
		$this->assertNotNull(json_decode('{"users": {"1": {"name": "test" },"10001": {"name":"pablo"}}}', TRUE));
		$this->assertNotNull(json_decode($j,TRUE));
	}

	public function testGetUsers()
	{
		$this->assertNotNull($this->dm->getUsers());
	}

	public function testGetUser()
	{
		$user = $this->dm->getUser('testuser1');
		$this->assertNotNull($user);
		$this->assertEquals($user->name, 'testuser1');
		$mb = $user->getMailboxes();
		$this->assertNotNull($mb['mailboxes']);
	}

	public function testGetMailbox()
	{
		$this->assertNotNull($this->mbox);
	}

	public function testAddUser()
	{
		$user = $this->dm->getUser(null);
		//FIXME: $user->create('testadduser','testaddpassword');
	}

	public function testDelUser()
	{
		$user = $this->dm->getUser('testadduser');
		$user->delete();
	}

	public function testAddMailbox()
	{
		$this->user->addMailbox("somenewmailbox1");
		$this->assertNotNull($this->user->getMailbox("somenewmailbox1"));
	}

	public function testDelMailbox()
	{
		$this->assertNotNull($this->user->getMailbox("somenewmailbox2"));
		$this->user->delMailbox("somenewmailbox2");
		$this->assertEquals($this->user->getMailbox("somenewmailbox2"), null);
	}

	public function testGetMessages()
	{
		$msgs = $this->mbox->getMessages();
		$this->assertNotNull($msgs);
	}

	public function testGetMessage() 
	{
		$msgs = $this->mbox->getMessages();
		$keys = array_keys($msgs['messages']);
		$this->assertNotNull($keys[0]);
		$msg = $this->mbox->getMessage($keys[0]);
		$this->assertNotNull($msg->view());
	}

	public function testGetHeaders()
	{
		$msgs = $this->mbox->getMessages();
		$keys = array_keys($msgs['messages']);
		$this->assertNotNull($keys[0]);
		$msg = $this->mbox->getMessage($keys[0]);
		$headers = $msg->getHeaders(array('subject','to','received'));
		$this->assertNotNull($headers);
	}

	public function testAddMessage()
	{
		$user = $this->dm->getUser('testuser1');
		$mbox = $user->getMailbox('INBOX');
		$exists = $mbox->exists;
		$msg = "From: testuser1@localhost\nTo: testuser1@localhost\nSubject: test\n\ntext message\n\n";
		$mbox->addMessage($msg);
		$mbox = $user->getMailbox('INBOX');
		$this->assertEquals($mbox->exists > $exists,true);
	}

	public function testDeleteMessage()
	{

	}

	public function testUpdateMessage()
	{

	}

}
?>
