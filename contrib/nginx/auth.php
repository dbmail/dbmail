<?php

/*
 * PHP5 authentication script for the NGINX 
 * mail proxy module.
 *
 * see: http://wiki.nginx.org/ImapProxyExample
 *
 * copyright 2013, Paul J Stevens <paul@nfg.nl>
 *
 * license: GPLv3
 *
 * requirement: php5, php5-imap
 *
 * setup:
 *
 * - edit the configuration below
 * - make sure the script is reachable over HTTP
 * - test the script:
 *
 * say you have a 'testuser1' with password 'test',
 * and the scripts is reachable at http://localhost/auth.php
 *
 * GET -e -H 'Auth-User: testuser1' -H 'Auth-Pass: test' \
 * 	  -H 'Auth-Protocol: imap' http://localhost/auth.php
 *
 * this should return something like:
 
 200 OK
 Connection: close
 Date: Wed, 11 Dec 2013 07:39:28 GMT
 Server: Apache/2.2.22 (Ubuntu)
 Content-Length: 0
 Content-Type: text/html
 Auth-Port: 10143
 Auth-Server: localhost
 Auth-Status: OK
 Client-Date: Wed, 11 Dec 2013 07:39:28 GMT
 Client-Peer: 127.0.0.1:80
 Client-Response-Num: 1
 X-Powered-By: PHP/5.4.6-1ubuntu1.4

 *
 *
 */

/* 
 *
 * START configuration 
 *
 * modify below settings to match your setup
 *
 */

/* If you want to support APOP authentication
 * this script will need to be able to retrieve 
 * the plain-text password from the database
 */

// database connection parameters
define("DBTYPE", "postgresql");
//define("DBTYPE", "mysql");
define("DBHOST", "127.0.0.1");
define("DBNAME", "dbmail");
define("DBUSER", "dbmail");
define("DBPASS", "dbmail");

// the port your DBMail IMAP server listens on 
$DBMAIL['IMAP'] = 10143;

// the port your DBMail POP3 server listens on
$DBMAIL['POP3'] = 10110;

/* the hosts running the DBMail services.
 * 
 * examples:
 *
 * single backend:
 *
 * $DBMAIL['HOST'] = 'localhost';
 * $DBMAIL['HOST'] = array('localhost');
 *
 * multiple backends:
 *
 * $DBMAIL['HOST'] = array('10.1.1.1', '10.1.1.2');
 */
$DBMAIL['HOST'] = array('127.0.0.1');
 
/*
 * 
 * END configuration
 *
 *
 */

if (!isset($_SERVER["HTTP_AUTH_USER"] ) || !isset($_SERVER["HTTP_AUTH_PASS"] )){
	fail("invalid request");
}

function fail($msg=''){
	header("Auth-Status: Invalid login or password");
	if ($msg) {
		header("Auth-Message: $msg");
	}
	exit;
}    

function pass($server,$port){
	header("Auth-Status: OK");
	header("Auth-Server: $server");
	header("Auth-Port: $port");
} 

function get_password($username) 
{
	# APOP needs access to the plain text passwords in the database.
	#
	#
	#
	# POSTGRESQL
	if (DBTYPE=="postgresql") {
		$conn = pg_connect(sprintf("dbname=%s host=%s user=%s password=%s",
			DBNAME, DBHOST, DBUSER, DBPASS));
		if (! $conn)
			fail();

		$result = pg_prepare($conn, "clearpw", 'SELECT passwd FROM dbmail_users WHERE userid = $1');
		$result = pg_execute($conn, "clearpw", array($username));
		if (! $result)
			fail();
		$row = pg_fetch_row($result);
		if (! $row)
			fail();
		return $row[0];
	}

	# MySQLi
	if (DBTYPE=="mysql") {
		$conn = new mysqli(DBHOST, DBUSER, DBPASS, DBNAME);
		if ($conn->connect_errno)
			fail();

		$stmt = $conn->prepare('SELECT passwd FROM dbmail_users WHERE userid = ?');
		if (! $stmt)
			fail();
		$stmt->bind_param('s', $username);
		$stmt->execute();
		$stmt->bind_result($password);
		$stmt->fetch();
		$stmt->close();
		return $password;
	}
}

function verify_hash($password, $hash, $salt) 
{
	$md5 = md5($salt . $password);
	return ($md5 == $hash);
}


$protocol=$_SERVER["HTTP_AUTH_PROTOCOL"] ;
if ($_SERVER["HTTP_AUTH_METHOD"] == "apop") {
	$username = $_SERVER["HTTP_AUTH_USER"];
	$userpass = get_password($username);
	if ($userpass) {
		$hash = $_SERVER["HTTP_AUTH_PASS"];
		$salt = $_SERVER["HTTP_AUTH_SALT"];
		if (verify_hash($userpass, $hash, $salt)) {
			get_mailserver($DBMAIL, $protocol);
			header("Auth-Pass: $userpass");
		} else {
			fail();
		}
	} else {
		fail();
	}
} else {
	get_mailserver($DBMAIL, $protocol);
}


function get_mailserver($config, $protocol)
{
	// default backend port
	$port=$config['POP3'];
	if ($protocol=="imap")
		$port=$config['IMAP'];

	if (! isset($config['HOST']))
		fail("config invalid");

	if (is_array($config['HOST'])) {

		/*
		 * backend selection is random, no fail-over, creating
		 * a simple load-balancing.
		 */

		$length = count($config['HOST']);
		if ($length == 1) {
			$host = $config['HOST'][0];
		} elseif ($length > 1) {
			$select = rand(0, $length-1);
			$host = $config['HOST'][$select];
		} else {
			fail("config invalid");
		}
	} elseif (is_string($config['HOST'])) {
		$host = $config['HOST'];
	} else {
		fail("config invalid");
	}
	pass($host, $port);
}

// END

?>
