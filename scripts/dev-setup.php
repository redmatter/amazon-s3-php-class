#!/usr/bin/php
<?php

/**
 * Updates
 *
 * PHP version 5
 *
 * @category  Updates
 * @package   RedMatter_TestHarness
 * @author    Dino Korah <Dino.Korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

define('SYSPATH', 'bla');

require_once 'syslogger/class.SysLogger.php';
require_once 'restclient/RestClient.class.php';
require_once '/var/www/API/application/libraries/XPath.php';
require_once '/var/www/API/application/libraries/DOMMaker.php';

SysLogger::Init('rm-dev-setup');

/**
* Request
*
* @category  Library
* @package   RedMatter_Request
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class API
{
	private $_baseUrl;
	private $_key;

	/**
	 * Ctor
	 * 
	 * @param string $baseUrl  base url
	 * @param string $username username
	 * @param string $password password
	 */
	public function __construct($baseUrl, $username, $password)
	{
		$this->_baseUrl = $baseUrl;
		
		$hash = urlencode(sha1(utf8_encode($username)).sha1(utf8_encode($password)));
		
		$res = $this->_RestCall('GET', "/auth/$hash", null, array('UIVERSION' => 'DevSetupScript:0.1'), null, null, null, 201);
		$xpath = new XPath($res->getResponse());
		if (!is_null($this->_key = $xpath('/Success/ResourceID')))
			return;
		
		MessageLog('CoreAPI Auth failed', 1);
	}
	
	/**
	 * Make the rest call
	 *
	 * @param string $method      method
	 * @param string $url         url
	 * @param string $body        body
	 * @param array  $headers     headers
	 * @param string $user        user
	 * @param string $pwd         password
	 * @param string $contentType content-type
	 * @param mixed  $expectation expectation
	 * 
	 * @throws Exception
	 * 
	 * @return RestClient
	 */
	private function _RestCall($method, $url, $body=null, $headers=null, $user=null, $pwd=null, $contentType=null, $expectation=null)
	{
		$url = $this->_baseUrl.$url;
	
		LogDebug($method, 'Method');
		LogDebug($url, 'URL');
		if (isset($body))
			LogDebug($body, 'Body');
		
		if (isset($headers))
		{
			LogDebug($headers, 'Headers');
			if (!isset($headers['Host']))
				$headers['Host'] = 'api';
		}
		else
			$headers = array('Host' => 'api');
	
		$res = RestClient::call($method, $url, $body, $headers, $user, $pwd, $contentType);
	
		LogDebug($res->getResponseCode(), 'Code');
		LogDebug($res->getResponse(), 'Body');
		LogDebug($res->getResponseContentType(), 'ContentType');
		LogDebug($res->getResponseMessage(), 'Message');
		
		if (!$this->_checkExpectation($res, $expectation))
			throw new Exception('Unexpected response from API');
	
		return $res;
	}

	/**
	 * Check exception
	 *
	 * @param RestClient $res         mr Resty
	 * @param mixed      $expectation what do you expect?
	 *
	 * return bool
	 *
	 * @return bool|mixed
	 */
	private function _checkExpectation(RestClient $res, $expectation)
	{
		if (is_null($expectation))
			return true;
		
		$code = $res->getResponseCode();
		
		if (is_callable($expectation))
			return call_user_func_array($expectation, array($res));
		
		elseif (is_array($expectation))
			return in_array($code, $expectation);
		
		elseif (is_numeric($expectation))
			return $code == $expectation;
		
		return false;
	}

	/**
	 * Do a GET
	 *
	 * @param string $url         url
	 * @param null   $expectation expectation
	 * @param bool   $useAuth     auth?
	 *
	 * @return mixed
	 */
	public function Get($url, $expectation=null, $useAuth=true)
	{
		return $this->_RestCall('GET', $url, null, $useAuth? array('Authorization'=>$this->_key): null, null, null, 'application/xml', $expectation);
	}

	/**
	 * Do a POST
	 *
	 * @param string $url         url
	 * @param string $body        body
	 * @param null   $expectation expectation
	 * @param bool   $useAuth     auth?
	 *
	 * @return mixed
	 */
	public function Post($url, $body, $expectation=null, $useAuth=true)
	{
		return $this->_RestCall('POST', $url, $body, $useAuth? array('Authorization'=>$this->_key): null, null, null, 'application/xml', $expectation);
	}

	/**
	 * Do a PUT
	 *
	 * @param string $url         url
	 * @param string $body        body
	 * @param null   $expectation expectation
	 * @param bool   $useAuth     auth?
	 *
	 * @return mixed
	 */
	public function Put($url, $body, $expectation=null, $useAuth=true)
	{
		return $this->_RestCall('PUT', $url, $body, $useAuth? array('Authorization'=>$this->_key): null, null, null, 'application/xml', $expectation);
	}
	
	/**
	 * Destroyer!
	 */
	public function __destruct()
	{
		if (isset($this->_key))
			$this->_RestCall('DELETE', '/auth', null, array('Authorization'=>$this->_key));
	}
}

/**
 * Log a message and optionally quit
 *
 * @param string   $str      log message
 * @param bool|int $exitCode optional exit code
 *
 * @return void
 */
function MessageLog($str, $exitCode=false)
{
	echo "$str\n";
	if (($exitCode !== false) && is_numeric($exitCode))
		exit($exitCode);
}

/**
 * Run dbmigrate
 * 
 * @param string $schema schema
 * @param string $cmd    command
 * 
 * @return int exit-code
 */
function dbmigrate($schema, $cmd)
{
	runcmd("dbmigrate $schema $cmd <<yy\ny\nyy");
}

/**
 * run command with an expectation
 * 
 * @param string $cmd         command to run
 * @param int    $expectation expected status, if not matched, it will throw
 * 
 * @throws Exception
 * 
 * @return void
 */
function runcmd($cmd, $expectation=0)
{
	$ret = null;
	passthru($cmd, $ret);
	
	if ($expectation != $ret)
		throw new Exception("Failed: $cmd - Status: $ret");
}


// -------------------- here we start ------------------

try
{

	if (posix_getuid())
		throw new Exception("Must be run as root");
	
	if ($argc !== 2)
	{
		if (!(($argc === 3) && ($argv[1] == 'Archiving') && is_numeric($argv[2])))
			throw new Exception(sprintf("Usage: %s <App>", basename($argv[0])));
	}
	
	$app = $argv[1];
	
	switch ($app)
	{
		case 'Archiving':
			$org = $argv[2];
			
			$api = new API('http://api.qa.redmatter.com', 'neil.burgess@redmatter.com', 'letmein');
			
			try
			{
				$api->Get("/archiving/provisioning/{$org}", 200);
			}
			catch (Exception $e)
			{
				MessageLog("Org $org not provisioned; doing it now!");
				$api->Put('/archiving/provisioning', '<?xml version="1.0" encoding="UTF-8"?><Archiving><Provisioning><OrgID>'.$org.'</OrgID></Provisioning></Archiving>', 201);
			}
	 		
	 		$xpath = new XPath($api->Get("/archiving/policy/{$org}/0", 200)->getResponse());
			if (is_null($defaultPolicyId = $xpath('/Archiving/Policy/ID')))
				MessageLog('Unable to find PolicyID', 1);
			
	 		$xpath = new XPath($api->Get('/archiving/defaultpolicy/'.$org, 200)->getResponse());
	 		
	 		$policyOk = array($defaultPolicyId => true); // default policy is already looked up; so we are good with that
	 		
	 		/**
	 		 * Check policy and set it to null if bad
	 		 * 
	 		 * @param int $policyId
	 		 * 
	 		 * @return bool
	 		 */
	 		$checkPolicy = function(&$policyId) use ($api, $org, &$policyOk) {
	 			try
	 			{
	 				if (isset($policyOk[$policyId]))
	 					return $policyOk[$policyId];
	 				
	 				$api->Get("/archiving/policy/$org/$policyId", 200);
	 				$policyOk[$policyId] = true;
	 			}
	 			catch (Exception $e)
	 			{
	 				MessageLog("Invalid policy $policyId for Org $org");
	 				$policyId = null;
	 				$policyOk[$policyId] = false;
	 			}
	 			
	 			return $policyOk[$policyId];
	 		};
	 		
	 		if (!is_null($recordingDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="recording"]/@PolicyID')))
	 			$checkPolicy($recordingDefPolicy);
	 		
	 		if (!is_null($smsDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="sms"]/@PolicyID')))
	 			$checkPolicy($smsDefPolicy);
	 		
	 		if (!is_null($cdrDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="cdr"]/@PolicyID')))
	 			$checkPolicy($cdrDefPolicy);
	 		
	 		if (is_null($recordingDefPolicy) || is_null($smsDefPolicy) || ($org == 1 && is_null($cdrDefPolicy)))
	 		{
	 			$body = new DOMMaker('Archiving');
	 			$defPols = $body->nodeCreate('DefaultPolicies');
	 			$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'recording', 'PolicyID' => is_null($recordingDefPolicy)? $defaultPolicyId: $recordingDefPolicy));
	 			$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'sms', 'PolicyID' => is_null($smsDefPolicy)? $defaultPolicyId: $smsDefPolicy));
	 			if ($org == 1)
	 				$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'cdr', 'PolicyID' => is_null($cdrDefPolicy)? $defaultPolicyId: $cdrDefPolicy));
	 			
	 			$api->Post('/archiving/defaultpolicy/'.$org, $body->getDOMDocument()->saveXML(), 202);
	 		}

	 		// set a predictable but random Encryption Key for the Org
	 		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000001 for Org 1
	 		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000002 for Org 2
	 		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000003 for Org 3
	 		// ... etc
	 		runcmd(sprintf("/usr/bin/mysql -hlontos01 -uapiuser -phungryhippos -e 'UPDATE `Archiving`.`Provisioning` SET EncryptionKey=UNHEX(\"69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A%010d\") WHERE `OrgID` = %d LIMIT 1'", $org, $org));
	 			
			break;
			
		default:
			MessageLog('Unknown command', 1);
			break;
	}
}
catch (Exception $e)
{
	MessageLog($e->getMessage(), 1);
}
