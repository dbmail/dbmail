<?php

$user="testuser1";
$pass="123456123456";

$connlist = array();

for ($i = 0; $i < 10; $i++) {
	$c = imap_open("{localhost:10143/notls}", "$user", "$pass");
	$connlist[] = $c;
}

print_r($connlist);

?>
