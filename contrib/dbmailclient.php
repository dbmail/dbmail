<?php

# Map RESTful dbmail api to php classes
#
# Author  Paul Stevens - paul@nfg.nl
# copyright: NFG BV 2009-2010, support@nfg.nl
#
class DBMail
{
	private $curl;

	public function __construct($host='localhost', $port=41380, $login='', $password='')
	{
		$this->curl = new Curl();
		$this->curl->options = array(
			'CURLOPT_FAILONERROR'=>TRUE,
			'CURLOPT_USERPWD'=>$login . ":" . $password
			);
		$this->curl->url = sprintf("http://%s:%d", $host, $port);
	}

	public function getUsers()
	{
		return json_decode($this->curl->get(sprintf("%s/users/", $this->curl->url))->body, TRUE);
	}

	public function getUser($userid)
	{
		return new DBMailUser($this->curl,$userid);
	}


}

class DBMailUser
{
	public $controller = "users";

	public function __construct($curl, $id)
	{
		$this->curl = $curl;
		$this->id = $id;
		$json = $this->get();
		if ($json) {
			if (! is_int($id))
				$this->id = $this->getIdByName($json[$this->controller], $id);
			if (is_int($this->id) and array_key_exists($this->id, $json[$this->controller])) {
				foreach($json[$this->controller][$this->id] as $key => $value) {
					$this->$key = $value;
				}
			}
		}
	}

	public function __toString()
	{
		return print_r($this->get(),1);
	}

	private function getIdByName($array, $name)
	{
		$id = null;
		assert(is_array($array));
		foreach($array as $key => $val) {
			if ($val['name'] == $name) {
				$id = (int)$key;
				break;
			}
		}
		if (! is_int($id))
			return null;
		return (int)$id;
	}

	private function getUrl($method='')
	{
		$url = sprintf("%s/%s/", $this->curl->url, $this->controller);
		if ($this->id) {
			$url = sprintf("%s%s/", $url, $this->id);
			if ($method) {
				$url = sprintf("%s%s/", $url, $method);
			}
		}
		return $url;
	}
	public function get($method='', $json=True)
	{
		$url = $this->getUrl($method);
		$result = $this->curl->get($url);
		if ($result) {
			if ($json)
				$result = json_decode($result->body,TRUE);
			else
				$result = $result->body;
		}

		//if (! $result) print "FAILURE URL:[".$url."]\n";
		return $result;
	}
		
	public function post($vars=array())
	{
		$url = $this->getUrl();
		$result = $this->curl->post($url, $vars);
		if (! $result) {
			//print "FAILURE URL:[".$url."]\n";
			return;
		}
		$result = json_decode($result->body,TRUE);
		foreach($result[$this->controller][$this->id] as $key => $value) {
			$this->$key = $value;
		}
		return $result;
	}

	public function create($userid, $password)
	{
		return $this->post(array('create'=>$userid, 'password'=>$password));
	}

	public function delete()
	{
		return $this->post(array('delete'=>$this->id));
	}


	public function getMailboxes()
	{
		return $this->get("mailboxes");
	}

	public function getMailbox($id)
	{
		$realid = null;
		$mblist = $this->getMailboxes();
		if (! is_int($id)) {
			$id = $this->getIdByName($mblist['mailboxes'], $id);
		}
		if (! is_int($id)) return null;
		return new DBMailMailbox($this->curl,$id);
	}

	public function addMailbox($mailbox)
	{
		return $this->post(array('create'=>$mailbox));
	}

	public function delMailbox($mailbox)
	{
		return $this->post(array('delete'=>$mailbox));
	}

}

class DBMailMailbox extends DBMailUser
{
	public $controller = "mailboxes";

	public function getMessages()
	{
		return $this->get("messages"); 
	}

	public function getMessage($id)
	{
		return new DBMailMessage($this->curl, $id);
	}

	public function addMessage($message)
	{
		return $this->post(array('message'=>$message));
	}
}

class DBMailMessage extends DBMailMailbox
{
	public $controller = "messages";

	public function view()
	{
		return $this->get("view", False);
	}

	public function getHeaders($headers)
	{
		$headers = implode(",",$headers);
		return $this->get("headers/" . $headers, False);
	}
}

# Curl, CurlResponse
# Author  Sean Huber - shuber@huberry.com

class Curl 
{
	public $cookie_file;
	public $headers = array();
	public $options = array();
	public $referer = '';
	public $user_agent = '';

	protected $error = '';
	protected $handle;


	public function __construct() 
	{
		$this->cookie_file = realpath('.').'/curl_cookie.txt';
		$this->user_agent = isset($_SERVER['HTTP_USER_AGENT']) ?
			$_SERVER['HTTP_USER_AGENT'] :
			'Curl/PHP ' . PHP_VERSION . ' (http://github.com/shuber/curl/)';
	}

	public function delete($url, $vars = array()) 
	{
		return $this->request('DELETE', $url, $vars);
	}

	public function error() 
	{
		return $this->error;
	}

	public function get($url, $vars = array()) 
	{
		if (!empty($vars)) {
			$url .= (stripos($url, '?') !== false) ? '&' : '?';
			$url .= http_build_query($vars, '', '&');
		}
		return $this->request('GET', $url);
	}

	public function post($url, $vars = array()) 
	{
		return $this->request('POST', $url, $vars);
	}

	public function put($url, $vars = array()) 
	{
		return $this->request('PUT', $url, $vars);
	}

	protected function request($method, $url, $vars = array()) 
	{
		$this->handle = curl_init();

		# Set some default CURL options
		curl_setopt($this->handle, CURLOPT_COOKIEFILE, $this->cookie_file);
		curl_setopt($this->handle, CURLOPT_COOKIEJAR, $this->cookie_file);
		curl_setopt($this->handle, CURLOPT_FOLLOWLOCATION, true);
		curl_setopt($this->handle, CURLOPT_HEADER, true);
		curl_setopt($this->handle, CURLOPT_POSTFIELDS, (is_array($vars) ? http_build_query($vars, '', '&') : $vars));
		curl_setopt($this->handle, CURLOPT_REFERER, $this->referer);
		curl_setopt($this->handle, CURLOPT_RETURNTRANSFER, true);
		curl_setopt($this->handle, CURLOPT_URL, $url);
		curl_setopt($this->handle, CURLOPT_USERAGENT, $this->user_agent);

		# Format custom headers for this request and set CURL option
		$headers = array();
		foreach ($this->headers as $key => $value) {
			$headers[] = $key.': '.$value;
		}
		curl_setopt($this->handle, CURLOPT_HTTPHEADER, $headers);

		# Determine the request method and set the correct CURL option
		switch ($method) {
		case 'GET':
			curl_setopt($this->handle, CURLOPT_HTTPGET, true);
			break;
		case 'POST':
			curl_setopt($this->handle, CURLOPT_POST, true);
			break;
		default:
			curl_setopt($this->handle, CURLOPT_CUSTOMREQUEST, $method);
		}

		# Set any custom CURL options
		foreach ($this->options as $option => $value) {
			curl_setopt($this->handle, constant('CURLOPT_'.str_replace('CURLOPT_', '', strtoupper($option))), $value);
		}

		$response = curl_exec($this->handle);
		if ($response) {
			$response = new CurlResponse($response);
		} else {
			$this->error = curl_errno($this->handle).' - '.curl_error($this->handle);
		}
		curl_close($this->handle);
		return $response;
	}

}

class CurlResponse 
{
	public $body = '';
	public $headers = array();

	public function __construct($response) 
	{
		# Extract headers from response
		$pattern = '#HTTP/\d\.\d.*?$.*?\r\n\r\n#ims';
		preg_match_all($pattern, $response, $matches);
		$headers = split("\r\n", str_replace("\r\n\r\n", '', array_pop($matches[0])));

		# Extract the version and status from the first header
		$version_and_status = array_shift($headers);
		preg_match('#HTTP/(\d\.\d)\s(\d\d\d)\s(.*)#', $version_and_status, $matches);
		$this->headers['Http-Version'] = $matches[1];
		$this->headers['Status-Code'] = $matches[2];
		$this->headers['Status'] = $matches[2].' '.$matches[3];

		# Convert headers into an associative array
		foreach ($headers as $header) {
			preg_match('#(.*?)\:\s(.*)#', $header, $matches);
			$this->headers[$matches[1]] = $matches[2];
		}

		# Remove the headers from the response body
		$this->body = preg_replace($pattern, '', $response);
	}

	public function __toString() 
	{
		return $this->body;
	}
}


?>
