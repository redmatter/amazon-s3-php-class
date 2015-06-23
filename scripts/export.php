#!/usr/bin/php
<?php

/**
* ArchivingExportApp
*
* PHP version 5
*
* @category Library
* @package  ClassAutoLoader
* @author   Greg Inglis <greg.inglis@redmatter.com>
* @license  http://www.redmatter.com/license private
* @version  SVN: $Id: $
* @link     http://trac.redmatter.com/
*
*/
define('APP_NAME_', 'rm-archexport');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);
//define('RM_CLI_APP', true);

require_once 'rmapp/class.BaseApp.php';
require_once 'rmdaemonscripts/DaemonScripts.php';
require_once 's3/S3.php';
require_once 'archiving/class.ArchivingProcessFile.php';
require_once 'restclient/RestClient.class.php';
require_once 'hPHP/hapi.php';
require_once 'eventlog/class.EventLog.php';
require_once 'rmbranding/Brand.php';

function formatTime($time, $timeZone, $format)
{
	$dt = new DateTime();
	$dt->setTimezone($timeZone);
	$dt->setTimestamp($time);
	return $dt->format($format);
}
	
class TruncateExportException extends Exception
{
	public function __construct()
	{
		parent::__construct();
	}
}

abstract class ArchivingExport
{
	protected $app = null;
	protected $exportID = null;
	protected $orgID = null;
	protected $userID = null;
	protected $queryQID = null;
	protected $resultIDs = null;
	protected $zipPassword = null;
	protected $reason = null;
	
	protected $encryptionKey = null;
	protected $branding = null;
	protected $policyID = null;
	protected $userDetails = null;
	protected $userTimezone = null;
	
	protected $policyPermissions = array();
	protected $policyNotificationList = array();
	
	protected $baseDir = null;
	protected $csvFileName = null;
	protected $csvFilePath = null;
	protected $csvFile = false;
	protected $zipFileName = null;
		
	protected $endpointDetails = array();
	protected $endpoints = array();
	
	protected $exportedPolicyIDs = array();
	protected $exportedMediaUUIDs = array();
	
	const TEMP_DIR = '/tmp/rmarchexport';
	const TEMP_CLI_DIR = '/tmp/rmarchexport-cli';
		
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword, $reason)
	{
		$this->app = $app;
		$this->exportID = (int)$exportID;
		$this->orgID = (int)$orgID;
		$this->userID = (int)$userID;
		$this->queryQID = (int)$queryQID;
		$this->resultIDs = $resultIDs;
		$this->zipPassword = (string)$zipPassword;
		$this->reason = (string)$reason;
		
		if ($this->exportID == 0)
			throw new Exception("No Export ID specified");
		if ($this->orgID == 0)
			throw new Exception("No Org ID specified");
		if ($this->userID == 0)
			throw new Exception("No User ID specified");
		if ($this->queryQID == 0)
			throw new Exception("No Query QID specified");
		if (empty($this->zipPassword))
			throw new Exception("No zip password specified");
	}
		
	public function __destruct()
	{
		if ($this->csvFile !== false)
			fclose($this->csvFile);
		
		if (!$this->app->cliMode && is_dir(self::TEMP_DIR))
			$this->cleanDirectory(self::TEMP_DIR);
	}
	
	abstract public function displayType();
	public function type() { return str_replace('/', '-', strtolower($this->displayType())); }
	
	abstract protected function archivingType();
	
	public function generate()
	{
		$this->encryptionKey = null;
		$this->branding = null;
		$this->policyID = null;
		$this->userDetails = null;
		$this->userTimezone = null;
		
		$this->policyPermissions = array();
		$this->policyNotificationList = array();
	
		$this->baseDir = null;
		$this->csvFileName = null;
		$this->csvFilePath = null;
		$this->csvFile = false;
		$this->zipFileName = null;
		
		$this->endpointDetails = array();
		$this->endpoints = array();
		
		$this->exportedPolicyIDs = array();
		$this->exportedMediaUUIDs = array();
					
		LogMessage("Starting ".$this->displayType()." log export: ExportID: {$this->exportID}, OrgID: {$this->orgID}, UserID: {$this->userID}, QueryQID: {$this->queryQID}, ResultIDCount: ".count($this->resultIDs) . ", Reason: {$this->reason}");
		LogDebug("Password: {$this->zipPassword}");
				
		// Get info/settings for this user/org
		
		$this->encryptionKey = $this->getOrgEncryptionKey($this->orgID);
		$this->branding = $this->getOrgBranding($this->orgID);
				
		$this->policyID = $this->getOrgDefaultPolicyID($this->orgID);
		$this->userDetails = $this->getUserDetails($this->userID);
		$this->userTimezone = $this->getUserTimeZone($this->userID);
				
		// OK and now the actual API call logs

		$searchStartTime = false;
		$searchEndTime = false;
		$callLogsData = array();
		$this->getApiLogsData($searchStartTime, $searchEndTime, $callLogsData);

		$searchStartTime = formatTime($searchStartTime, $this->userTimezone, 'Y-m-d@H.i.s');
		$searchEndTime = formatTime($searchEndTime, $this->userTimezone, 'Y-m-d@H.i.s');

		//echo "logsData:\n".print_r($logsData);
		
		// Get archive retention details from big DB

		$dbRetResults = $this->getLogsAndRetentionDbResults();
		if (empty($dbRetResults))
			throw new Exception("Failed to fetch retention database results for query QID {$this->queryQID}");

		LogMessage("Processing ".count($dbRetResults)." ".$this->displayType()." log entries...");
		
		// set up/clean temporary directory where we will stick CSV, WAV, MMS (.eml) and zip files
		
		$tempDir = $this->app->cliMode ? self::TEMP_CLI_DIR : self::TEMP_DIR;
		if (is_dir($tempDir))
			$this->cleanDirectory($tempDir);	
		$this->baseDir = "{$tempDir}/{$this->exportID}";		
		// ensure tmp directory for this export ID is removed first
		if (!$this->createDirectoryTree($this->baseDir))
			throw new Exception("Failed to create temp directory '{$this->baseDir}'");
			
		// format up file names and create export CSV file with column header names
			
		$this->csvFileName = "org-{$this->orgID}-{$this->type()}-export-{$this->exportID}-from-{$searchStartTime}-to-{$searchEndTime}";
		$this->csvFilePath = "{$this->baseDir}/{$this->csvFileName}.csv";
		$this->zipFileName = "{$this->csvFileName}.zip";

		$this->csvFile = fopen($this->csvFilePath, 'wb');
		if ($this->csvFile === false)
			throw new Exception("Failed to open CSV file for writing '{$this->csvFilePath}'");

		$csvHeaderLine = implode(',', $this->getCsvHeaderFields());
		if (fwrite($this->csvFile, $csvHeaderLine."\r\n") === false)
			throw new Exception("Failed to write header to CSV file '{$this->csvFilePath}'");

		$result = false;
		
		if (!empty($this->resultIDs))
			$totalCount = count($this->resultIDs); 
		else
			$totalCount = count($callLogsData);
			
		// righto off we go! for each log entry...

		$exportedCount = 0;
		$fullyExportedCount = 0;
		foreach ($dbRetResults as $dbRetResult)
		{
			$resID = (int)$dbRetResult['ResID'];
			if (empty($this->resultIDs) || isset($this->resultIDs[$resID]))
			{	
				$callLogData = &$callLogsData[$resID];
								
				try
				{
					// returns false if media was too big to download or would put export zip file size over size limit 
					if ($this->exportLogEntry($callLogData, $dbRetResult))
						++$fullyExportedCount;
					++$exportedCount;
					
					if ($this->exportID > 0)
					{
						$progress = "... {$exportedCount} of {$totalCount}";	
						$this->app->updateExportStatus($this->exportID, 'Progress', $progress);
					}
				}
				catch (TruncateExportException $ex)
				{
					// Zip file size limit would be exceeded - truncate export before this entry
					LogWarning("Zip archive would exceed {$app->this->maxZipFileSizeMB} MB file size limit - truncating export zip before result ID {$resID}", null, 'U:ARCX');
					
					$result = "OK but truncated ({$exportedCount} of {$totalCount} entries exported)";					
					break;
				}
				++$exportedCount;	
			}
		}
		
		// if we haven't managed to fully export any entries (due to media being too big), then set the overall export job status to failed	
		if (($totalCount > 0) && ($fullyExportedCount == 0))
			throw new Exception("Failed to generate export for query QID {$this->queryQID} - all entries are too big");
				
		if (!$result)
		{
			if ($exportedCount == 1)
				$result = "OK (1 entry exported)";
			else
				$result = "OK ({$exportedCount} entries exported)";
		}

		fclose($this->csvFile);
		$this->csvFile = false;
		
		$zipFilePath = "{$this->baseDir}/{$this->zipFileName}";
	
		// add the CSV file to the zip
		$this->addToZipArchive("{$this->csvFileName}.csv");
	
		// upload the final zip to S3
		$storageKey = "export/{$this->orgID}/{$this->exportID}/1";
		$this->uploadZipArchive($zipFilePath, $storageKey);
	
		// Having a UUID in the export URL is just to provide a bit of obfuscation 
		// and to give the appearance that export download URLs are as secure/unguessable as recording/SMS/MMS download URLs
		$exportUUID = trim(exec('/usr/bin/uuidgen -t'));
		
		$exportURI = "export/{$this->orgID}/{$exportUUID}";
		$res = new stdClass();
		$res->policyID = $this->policyID;
		$res->exportURI = $exportURI;
		$res->result = $result;
		
		$this->writeEventLogSuccess($exportURI);
		
		return $res;
	}
	
	abstract protected function exportLogEntry(array &$logData, array &$res);

	protected function getUserTimeZone($userID)
	{
		$client = RestClient::createClient();
		$client->setUrl('http://api.'.$this->app->hAPISearchDomain.'/profiles/User/'.$userID);
		$client->setMethod('GET');
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);

		$client->execute();
		
		$profileResponse = trim($client->getResponse());
		if (empty($profileResponse))
			throw new Exception("Failed to fetch user profile details for user ID {$userID}");
			
		if ($client->getResponseCode() != 200)	
			throw new Exception("Failed to fetch user profile details for user ID {$userID}, response: {$profileResponse}");
		
		//echo "profilePesponse:\n{$profileResponse}\n";

		$profileResponseDom = simplexml_load_string($profileResponse);
		if (!($profileResponseDom instanceof SimpleXMLElement)) 
			throw new Exception("Failed to fetch user profile details for user ID {$userID}, response: {$profileResponse}");
		
		$timezone = null;
		$timezoneValNode = $profileResponseDom->xpath('General[1]/DefTZ[1]/@Val');
		if (!empty($timezoneValNode)) 
			$timezone = (string)$timezoneValNode[0];
		
		if (empty($timezone))
		{
			$timezoneDefaultNode = $profileResponseDom->xpath('General[1]/DefTZ[1]/@Default');
			if (!empty($timezoneDefaultNode))
				$timezone = (string)$timezoneDefaultNode[0];
		}
		
		if (empty($timezone))
		{
			$timezone = 'Europe/London';
			LogMessage("Couldn't determine time zone for user ID {$userID} - defaulting to {$timezone}");
		}
		
		return new DateTimeZone($timezone);
	}

	protected function getUserDetails($userID)
	{
		$client = RestClient::createClient();
		$client->setUrl('http://api.'.$this->app->hAPISearchDomain.'/users/'.$userID);
		$client->setMethod('GET');
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);

		$client->execute();
		
		$usersResponse = trim($client->getResponse());
		if (empty($usersResponse))
			throw new Exception("Failed to fetch details for user ID {$userID}");
			
		if ($client->getResponseCode() != 200)	
			throw new Exception("Failed to fetch details for user ID {$userID}, response: {$usersResponse}");
		
		//echo "usersResponse:\n{$usersResponse}\n";
		
		$usersResponseDom = simplexml_load_string($usersResponse);
		if (!($usersResponseDom instanceof SimpleXMLElement)) 
			throw new Exception("Failed to fetch details for user ID {$userID}, response: {$usersResponse}");
		
		$details = new stdClass();	
		$fields = array('UserName', 'FirstName', 'MiddleNames', 'LastName');
		foreach ($fields as $field)
		{			
			$fieldNode = $usersResponseDom->xpath('User[1]/@'.$field);
			if (empty($fieldNode)) 
				throw new Exception("Failed to extract field '{$field}' from details for user ID {$userID}, response: {$usersResponse}");
			$value = (string)$fieldNode[0];
			
			$field = strtolower($field);
			$details->{$field} = $value;
		}
				
		return $details;
	}
		
	abstract protected function getApiLogsPath();
	
	protected function getApiLogsData(&$searchStartTime, &$searchEndTime, array &$logsData)
	{
		$client = RestClient::createClient();
		$client->setUrl('http://api.'.$this->app->hAPISearchDomain.$this->getApiLogsPath());
		$client->setMethod('POST');	
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);

		$logsParamsXml = "<Parameters QID=\"{$this->queryQID}\" Max=\"8000\" Offset=\"0\" ShowMappings=\"YES\" UserFormat=\"first last\" ArchivingForUserID=\"{$this->userID}\"/>";
		$client->setParameters($logsParamsXml);
		
		$client->execute();
		
		$logsResponse = trim($client->getResponse());
		if (empty($logsResponse))	
			throw new Exception("Failed to fetch logs result for query QID {$this->queryQID}");		
		
		//echo "logsResponse:\n{$logsResponse}\n";
		
		if (($client->getResponseCode() != 200) && ($client->getResponseCode() != 201))
			throw new Exception("Failed to fetch logs result for query QID {$this->queryQID}, response: {$logsResponse}");
				
		$logsResponseDom = simplexml_load_string($logsResponse);
		if (!($logsResponseDom instanceof SimpleXMLElement))
			throw new Exception("Failed to fetch logs result for query QID {$this->queryQID}, response: {$logsResponse}");
		
		$searchStartTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[1]/@Val');
		$searchEndTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[2]/@Val');
		if (empty($searchStartTimeNodes) || empty($searchEndTimeNodes))
			throw new Exception("Failed to fetch logs search criteria for query QID {$this->queryQID}, response: {$logsResponse}");
		$searchStartTime = (int)$searchStartTimeNodes[0];
		$searchEndTime = (int)$searchEndTimeNodes[0];
		
		$this->processApiLogsData($logsResponseDom, $logsData);	
		if (empty($logsData))
			throw new Exception("Failed to fetch logs result for query QID {$this->queryQID}");
	}
	
	abstract protected function processApiLogsData(SimpleXMLElement $logsResponseDom, array &$logsData);
	
	protected function getOrgEncryptionKey($orgID)
	{
		$result = $this->app->coreDbc->query(
			"SELECT `EncryptionKey` FROM `Archiving`.`Provisioning` WHERE `OrgID`={$orgID} LIMIT 1"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		if (empty($result) || empty($result[0]) || empty($result[0]['EncryptionKey']))
			throw new Exception("Could not get encryption key for Org {$orgID}");

		return $result[0]['EncryptionKey'];	
	}
	
	protected function getOrgBranding($orgID)
	{
		$client = RestClient::createClient();
		$client->setUrl("http://api.{$this->app->hAPISearchDomain}/branding/{$this->orgID}");
		$client->setMethod('GET');	
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);
		
		$client->execute();
		
		$brandingResponse = trim($client->getResponse());
		
		//echo "brandingResponse:\n{$brandingResponse}\n";
		
		if (empty($brandingResponse))
			throw new Exception("Failed to query CoreAPI branding settings for org ID {$this->orgID}");
		
		if ($client->getResponseCode() != 200)
			throw new Exception("Failed to query CoreAPI branding settings for org ID {$this->orgID}, response: {$brandingResponse}");
		
		$brandingDom = simplexml_load_string($brandingResponse);
		if (!($brandingDom instanceof SimpleXMLElement))
			throw new Exception("Failed to query CoreAPI branding settings for org ID {$this->orgID}, response: {$brandingResponse}");
			
		$nodes = $brandingDom->xpath('@Identifier');
		if (empty($nodes))
			throw new Exception("Failed to extract 'Identifier' attribute from CoreAPI branding settings for org ID {$this->orgID}, response: {$brandingResponse}");
								
		$identifier = (string)$nodes[0];
		if (empty($identifier))
			throw new Exception("Failed to extract 'Identifier' attribute from CoreAPI branding settings for org ID {$this->orgID}, response: {$brandingResponse}");
		
		$branding = Brand::ByIdentifier($identifier);
		if (!is_object($branding))
			throw new Exception("Failed to find branding settings for '{$identifier}' for org ID {$this->orgID}, response: {$brandingResponse}");
		return $branding;
	}	
	
	protected function getOrgDefaultPolicyID($orgID)
	{
		$result = $this->app->coreDbc->query(
			"SELECT m.PolicyID ".
			"FROM Archiving.DefaultPolicyMap m ".
			"JOIN Archiving.PolicyTypes t ON m.PolicyTypesID=t.ID ". 
			"WHERE m.OrgID={$orgID} ". 
			"AND t.Name='".$this->archivingType()."' ".
			"AND t.Internal='no';"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		if (empty($result) || empty($result[0]) || empty($result[0]['PolicyID']))
			throw new Exception("Could not fetch default {$type} policy ID for Org {$orgID}");
		return (int)$result[0]['PolicyID'];	
	}
	
	abstract protected function getLogsAndRetentionDbResults();
	
	protected function checkArchivingPolicyPermission($policyID, $ownerUserID)
	{
		LogVerbose("Checking CoreAPI archiving policy permission for policy ID {$policyID}, Retention User ID {$ownerUserID}");
		
		if (!isset($this->policyPermissions[$policyID]))
		{
			$this->policyPermissions[$policyID] = array();
		}
		else if (isset($this->policyPermissions[$policyID][$ownerUserID]))
		{			
			$allowed = $this->policyPermissions[$policyID][$ownerUserID];
			LogDebug("Using previously cached permission result: ".($allowed?'true':'false'));
			return $allowed;
		}
				
		$client = RestClient::createClient();
		$client->setUrl("http://api.{$this->app->hAPISearchDomain}/archiving/policypermissions/{$this->orgID}/{$policyID}/{$this->userID}/{$ownerUserID}/read");
		$client->setMethod('GET');	
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);
		
		$client->execute();
		
		$permsResponse = trim($client->getResponse());
		
		//echo "permissionsResponse:\n{$permsResponse}\n";
		
		if (empty($permsResponse))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}");
		
		if ($client->getResponseCode() != 200)
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
		
		$permsDom = simplexml_load_string($permsResponse);
		if (!($permsDom instanceof SimpleXMLElement))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
			
		$nodes = $permsDom->xpath('@Allowed');
		if (empty($nodes))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
						
		LogDebug("Checked CoreAPI archiving policy permission for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");	
		
		$allowed = (string)$nodes[0];
		$allowed = ($allowed == 'YES');

		// stash away notification settings for subsequent custodian Email/SMS notifications
		if (!isset($this->policyNotificationList[$policyID]))
		{
			$this->policyNotificationList[$policyID] = array();
			foreach (array('Email', 'SMS') as $type)
			{
				$userIDs = array();				
				$userIDNodes = $permsDom->xpath("NotificationList[1]/{$type}[1]/User/@ID");
				foreach ($userIDNodes as $userIDNode)
					$userIDs[] = (int)$userIDNode[0];
				$this->policyNotificationList[$policyID][$type] = $userIDs;
			}
		}
							
		// cache 'allowed' result
		$this->policyPermissions[$policyID][$ownerUserID] = $allowed;
		return $allowed;
	}
	
	protected function getS3ObjectContent($policyID, $policyVersion, $storageKey, &$content)
	{
		// Get endpoint details from API
		$bucket = $this->getEndpointDetails($policyID, $policyVersion)->bucket;		
		$s3 = $this->getS3Endpoint($policyID, $policyVersion);
		
		// First check if size not too big
		
		LogVerbose("Getting S3 object info {$storageKey} from S3 Bucket {$bucket}");
		
		$infoResponse = $s3->getObjectInfo($bucket, $storageKey);
		if (empty($infoResponse) || empty($infoResponse['x-amz-meta-clear-size']) || empty($infoResponse['size']))
			throw new Exception("Failed to fetch {$storageKey} S3 object info");
	
		foreach ($infoResponse as $h => $v)
			LogDebug("header: '{$h}' = '{$v}'");
			
		$filesize = (double)$infoResponse['x-amz-meta-clear-size'];
		LogVerbose("Uncompressed object size (x-amz-meta-clear-size): {$filesize}");
		
		if ($filesize > ($this->app->maxFileSizeMB * (1024.0*1024.0)))
			// skip this exporting this entry, but keep going (as subsequent log entry media may still fit in the export zip
			return $filesize;	
		
		$amzCompressedFilesize = (double)$infoResponse['size'];
		LogVerbose("S3 compressed object size: {$amzCompressedFilesize}");
		
		// p7zip compression gives a file size that is about 15% bigger than the S3 compressed size
		$p7zipCompressedFilesize = $amzCompressedFilesize * 1.15;		
		if ($p7zipCompressedFilesize > ($this->app->maxZipFileSizeMB * (1024.0*1024.0)))
			// skip this exporting this entry, but keep going (as subsequent log entry media may still fit in the export zip
			return $filesize;
		
		// check zip file not too big	
		$zipFilePath = "{$this->baseDir}/{$this->csvFileName}.zip";
		if (file_exists($zipFilePath))
		{
			// OK REALLY REALLY important to call clearstatcache() first other was filesize() may return an earlier file size!:
			// 'On Linux based systems, data fetched by filesize() is "statcached".
			// Try calling clearstatcache(); before the filesize call.'
			// http://stackoverflow.com/questions/3747982/php-filesize-reporting-old-size			
			clearstatcache();
			
			$zipFilesize = filesize($zipFilePath);
			
			if (($zipFilesize > 0) && ($p7zipCompressedFilesize > 0) && (($zipFilesize+$p7zipCompressedFilesize) > ($this->app->maxZipFileSizeMB * (1024.0*1024.0))))
				// this would make zip file too big - truncate export before this entry
				throw new TruncateExportException();
		}	
		
		// size OK - download object from S3
		LogVerbose("Downloading {$storageKey} from S3 Bucket {$bucket}");
		
		$response = $s3->getObject($bucket, $storageKey);
		if (!isset($response->code) || $response->code !== 200)
		{
			$message = "There was a problem downloading {$storageKey} from S3";
			if (isset($response->error))
				$message .= ": {$response->error}";
			throw new Exception($message);
		}

		if (!isset($response->headers) || !is_array($response->headers))
			throw new Exception("{$storageKey} was successfully downloaded from S3 but had no headers; skipping");

		if (empty($response->body))
			throw new Exception("{$storageKey} GET was successful but no body was returned");

		LogVerbose("Successfully downloaded {$storageKey} from S3 Bucket {$bucket}");
		
		try
		{
			LogVerbose("Processing downloaded file content of {$storageKey}, size ".strlen($response->body) ." bytes");
			$content = ArchivingProcessFile::ProcessFileContent($response->body, $this->encryptionKey);
		}
		catch (Exception $e)
		{
			throw new Exception("Failed to process downloaded file content of {$storageKey}: {$e}");
		}
		
		return $filesize;
	}
		
	protected function getLogsIDMap(SimpleXMLElement $dom, $xpath, array &$map)
	{
		$nodes = $dom->xpath($xpath);
		if (!is_array($nodes))
			return;
		
		foreach ($nodes as $node) 
		{
			$idNode = $node->xpath('@I');
			$nameNode = $node->xpath('@N');
			$id = (string)$idNode[0];
			$name = (string)$nameNode[0];
			$map[$id] = $name;
		}
	}
	
	protected function addToZipArchive($relFileSpec)
	{
		chdir($this->baseDir);
				
		$cmd = "7za a {$this->zipFileName} {$relFileSpec}";
		
		clearstatcache();
		
		LogVerbose("Adding file {$relFileSpec} (".filesize($relFileSpec)." bytes) to export zip '{$this->zipFileName}' with command: {$cmd} -p********");
		
		$cmd .= " -p{$this->zipPassword}";
		
		$output = array();
		$return_var = 0;
		$zipRes = exec($cmd, $output, $return_var);
		
		$zipFilePath = "{$this->baseDir}/{$this->zipFileName}";
		if (!file_exists($zipFilePath))
		{
			LogVerbose("zip result: {$zipRes}");
			LogVerbose("returned: {$return_var}");
			foreach ($output as $line)
			{
				LogVerbose("zip output: {$line}");
			}
			throw new Exception("Failed to create zip file '{$zipFilePath}'");
		}
		
		LogDebug("zip result: {$zipRes}");
		LogDebug("returned: {$return_var}");
		foreach ($output as $line)
		{
			LogDebug("zip output: {$line}");
		}
		
		LogDebug("Added file {$relFileSpec} to export zip '{$this->zipFileName}' (".filesize($zipFilePath)." bytes) OK");		
	}
	
	protected function uploadZipArchive($zipFilePath, $storageKey)
	{
		$bucket = $this->getEndpointDetails($this->policyID, 0/*policyVersion*/)->bucket;
		$s3 = $this->getS3Endpoint($this->policyID, 0/*policyVersion*/);		
		
		// OK REALLY REALLY important to call clearstatcache() first as filesize() is called internally by S3::putObjectFile() and:
		// 
		// 'On Linux based systems, data fetched by filesize() is "statcached".
		// Try calling clearstatcache(); before the filesize call.'
		// http://stackoverflow.com/questions/3747982/php-filesize-reporting-old-size
		// 
		clearstatcache();
		
		LogVerbose("Uploading export zip {$storageKey} (".filesize($zipFilePath)." bytes) to bucket {$bucket} in S3...");
			
		$headers = array();
		if (!$s3->putObjectFile($zipFilePath, $bucket, $storageKey))
			throw new Exception("Failed to store file '{$zipFilePath}' in S3 bucket: {$bucket}, storageKey: {$storageKey}");
		
		LogVerbose("Uploaded export zip to S3 OK");
	}	
		
	protected function getEndpointDetails($policyID, $policyVersion)
	{
		if (!isset($this->endpointDetails[$policyID][$policyVersion]))
		{
			$policy = ArchivingPolicy::Get($this->app->api, $this->orgID, $policyID, $policyVersion);
			$endpointDetails = ArchivingEndpoint::Get($this->app->api, $this->orgID, $policy->storageEndpointID, true);

			if (!isset($this->endpointDetails[$policyID]))
				$this->endpointDetails[$policyID] = array();

			$this->endpointDetails[$policyID][$policyVersion] = $endpointDetails;
		}

		return $this->endpointDetails[$policyID][$policyVersion];
	}

	protected function getS3Endpoint($policyID, $policyVersion)
	{
		if (!isset($this->endpoints[$policyID][$policyVersion]))
		{
			$endpointDetails = $this->getEndpointDetails($policyID, $policyVersion);

			if ($endpointDetails->type != 's3')
				throw new Exception('Endpoint is not on Amazon S3');

			if (!isset($this->endpoints[$policyID]))
				$this->endpoints[$policyID] = array();
				
			LogDebug("new S3 endpoint for policy ID {$policyID}: accessKey: {$endpointDetails->accessKey}, secretKey: {$endpointDetails->secretKey}");
			
			$endpoint = new S3($endpointDetails->accessKey, $endpointDetails->secretKey);
			$this->endpoints[$policyID][$policyVersion] = $endpoint;
		}

		return $this->endpoints[$policyID][$policyVersion];
	}
	
	private function getBrandingSetting($key, $defaultValue)
	{
		if (!isset($this->branding))
			throw new Exception("No branding settings");
		
		if (isset($this->branding->{$key}))
		{
			$value = (string)($this->branding->{$key});
			if (!empty($value))
				return $value;
		}

		return $defaultValue;
	}
	
	protected function addAuditDetails($archEntry, $timeUTC)
 	{			
		if (!isset($this->exportedPolicyIDs[$archEntry->policyID]))
		{
			$this->exportedPolicyIDs[$archEntry->policyID] = $archEntry->policyID;
		}
		else
		{
			$exportCount = $this->exportedPolicyIDs[$archEntry->policyID];
			$this->exportedPolicyIDs[$archEntry->policyID] = ++$exportCount;
		}		
		
		$this->exportedMediaUUIDs[$archEntry->uuid] = $timeUTC;	
	}
			
	private function writeEventLogSuccess($exportURI)
	{
		$notify = array();		
		$notify[] = array(
			'Type' => 'Email',
			'Subject' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} is complete",
			'Heading' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} is complete",
			'To' => array($this->userID),
			'From' => array(
				$this->getBrandingSetting('Common/NameSimplified', 'Natterbox').' Archive',
				$this->getBrandingSetting('Emailer/FromEmailAddress', 'noreply@natterbox.com')),
			'Text' => "Your {$this->displayType()} Logs Bulk Download can be accessed <a href=\"".$this->getBrandingSetting('URL/FileService', 'https://fileservice.natterbox.com')."/{$exportURI}\">here</a>.\r\n"
		);
		
		if (!empty($this->exportedPolicyIDs) && !empty($this->policyNotificationList))
		{
			// we have exported recordings/SMS/MMSs from one or more policy IDs and there are custodians configured for one or more of these policies
			
			// build de-duplicated list of userIDs to notify by Email/SMS
			$typeUserIDs = array();
			foreach (array('Email', 'SMS') as $type)
			{
				$typeUserIDs[$type] = array();
				foreach ($this->exportedPolicyIDs as $policyID => $exportedCount)
				{	
					if (isset($this->policyNotificationList[$policyID][$type]))
					{
						$userIDs = $this->policyNotificationList[$policyID][$type];
						foreach ($userIDs as $userID)
						{
							if (!isset($typeUserIDs[$type][$userID]))
								$typeUserIDs[$type][$userID] = $userID;
						}
					}
				}
			}
			
			// this is a bit crap that the time in the email body is always BST,
			// but this is also exactly what we do when a recording/SMS/MMS is accessed and we send custodian email notifications
			// (so I'm just being consistently crap here!)
			$bstz = new DateTimeZone('Europe/London');
			//$timeOfExport = formatTime(time(), $bstz, '%e %B %Y %r');
			$timeOfExport = formatTime(time(), $bstz, 'j F Y h:i:s A');

			$name = 'Unknown User';
			if (isset($this->userDetails) && !empty($this->userDetails->firstname))
			{
				$name = $this->userDetails->firstname;
				if (!empty($this->userDetails->middlenames))
					$name .= " {$this->userDetails->middlenames}";
				if (!empty($this->userDetails->lastname))
					$name .= " {$this->userDetails->lastname}";
			}
						
			$fileTypeVerbose = ($this->archivingType()=='recording'?'call recording':'SMS/MMS');
			$exportedCount = count($this->exportedMediaUUIDs);
			
			$brief = "{$name} exported {$exportedCount} {$fileTypeVerbose} ".($exportedCount==1?'file':'files')." for Bulk Download ID {$this->exportID} on the {$timeOfExport}.";
			
			if (!empty($typeUserIDs['Email']))
			{
				$notify[] = array(
					'Type' => 'Email',
					'Subject' => 'Archive Export Custodian Alert',
					'Heading' => 'Archive Export Custodian Alert',
					'To' => $typeUserIDs['Email'],
					'From' => array(
						$this->getBrandingSetting('Common/NameSimplified', 'Natterbox').' Archive',
						$this->getBrandingSetting('Emailer/FromEmailAddress', 'noreply@natterbox.com')),
					'Text' => "You are receiving this email because you are a registered Data Custodian for archived {$fileTypeVerbose}s.

{$brief}".
(isset($this->reason)? "
The stated reason for the export was:
\"{$this->reason}\"":'')
				);
			}
		
			if (!empty($typeUserIDs['SMS']))
			{
				$notify[] = array(
					'Type' => 'SMS',
					'To' => $typeUserIDs['SMS'],
					'From' => substr(
						preg_replace(
							'/[^-a-zA-Z0-9 +]+/', '',
							$this->getBrandingSetting('Common/NameSimplified', 'Natterbox')
						), 0, 11
					),
					'Text' => $brief.(isset($this->reason)? " Stated reason was: {$this->reason}.": '')
				);
			}
		}
						
		$message = "{$this->displayType()} Logs Bulk Download request ID {$this->exportID} was successfully processed; Reason for access: \"{$this->reason}\"";
		
		// ExtraData field in EventLogs.EventLog table is 65536 long so we might have to spread details of exportedMediaUUIDs across multiple event log entries.
		// Lets say log no more than 500 UUIDs (& call/SMS time) in each log entry
		
		$exportedMediaUUIDs = $this->exportedMediaUUIDs;		
		$firstTimeIn = true;		
		do
		{
			$batchExportedMediaUUIDs = array_slice($exportedMediaUUIDs, 0, 500, true/*preserveKeys*/); 
			$exportedMediaUUIDs = array_slice($exportedMediaUUIDs, 500, null, true/*preserveKeys*/);
			
			$extraData = array(
				'ExportID' => $this->exportID,
				'ExportedMedia' => $batchExportedMediaUUIDs
			);
			if ($firstTimeIn)
				$extraData['Notify'] = $notify;
							
			EventLog::Message(
				$this->orgID,
				'Archiving',
				"Archiving:{$this->displayType()}:Export:Complete",
				$message,
				$this->userID,
				null,
				$extraData
			);
			
			if ($firstTimeIn)
				$message = "(continued) {$message}";
			$firstTimeIn = false;
		}	
		while (count($exportedMediaUUIDs) > 0);		
	}
	
	public function writeEventLogFailed()
	{
		$notify = array();		
		$notify[] = array(
			'Type' => 'Email',
			'Subject' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} processing failed",
			'Heading' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} processing failed",
			'To' => array($this->userID),
			'From' => array(
				$this->getBrandingSetting('Common/NameSimplified', 'Natterbox').' Archive',
				$this->getBrandingSetting('Emailer/FromEmailAddress', 'noreply@natterbox.com')),
			'Text' => "The system was unable to process this {$this->displayType()} Logs Bulk Download request. Please try a different logs search range or contact your administrator.\r\n"
		);
								
		$message = "{$this->displayType()} Logs Bulk Download request ID {$this->exportID} processing failed";									
		EventLog::Error(
			$this->orgID,
			'Archiving',
			"Archiving:{$this->displayType()}:Export:Failed",
			$message,
			$this->userID,
			null,
			array(
				'ExportID' => $this->exportID,
				'Notify' => $notify,
			)
		);
	}	

	protected function createDirectoryTree($dirPath)
	{
		if (strlen($dirPath) == 0)
		{
			return false;
		}
		
		$path = '';
		
		if ($dirPath[0] == '/')
		{
			$path = '/';
			$dirPath = substr($dirPath, 1);
		}
		
		$dirs = explode('/', $dirPath);
		foreach ($dirs as $dir)
		{
			$path .= $dir;
			
			if (!is_dir($path))
			{
				$oldmask = umask(0);
				$success = (bool)mkdir($path, 0700);
				umask($oldmask);
				if (!$success)
				{
					return false;
				}
			}

			$path .= '/';
		}

		return true;
	}

	protected function cleanDirectory($dir)
	{
		if (strpos($dir, '/tmp/') !== 0) {
			LogWarning("Attempting to clean non-temp directory?");
			return;
		}
		exec("rm -R -f {$dir}/");
	}
	
	abstract protected function getCsvHeaderFields();
}

class ArchivingCallExport extends ArchivingExport
{
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword, $reason)
	{
		parent::__construct($app, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword, $reason);
	}
	
	public function displayType() { return 'Call'; }
	protected function archivingType() { return 'recording'; }
	
	protected function getApiLogsPath()
	{
		return '/newlogs/download';
	}	
	
	protected function exportLogEntry(array &$logData, array &$res)
	{
		$AUUID = $logData['AUUID'];
		$BUUID = $logData['BUUID'];			
		$time = (int)$logData['Time'];
		$fromNumber = $logData['From_Number'];
		$toNumber  = $logData['To_Number'];
		$recorded = $logData['Recorded'];
		$resID = (int)$res['ResID'];
		$startTimeUTC = gmstrftime('%F %T', $time);
			
		LogVerbose("Processing logs result ID {$resID}, AUUID: {$AUUID}, Recorded: '{$recorded}'...");
		
		$success = true;
		
		$recording = '"None"';
		$recordingTime = '';
		$recordingSize = '';
		if ($recorded == 'YES')
		{
			$prefix = formatTime($time, $this->userTimezone, 'Y-m-d@H.i.s') . "-from-{$fromNumber}-to-{$toNumber}";
			$archRec = $this->getArchivedRecording($logData, $res, $prefix);
			if (is_object($archRec))
			{
				LogVerbose("Fetched recording for result ID {$resID}: {$archRec->filepath}");
								
				$this->addToZipArchive($archRec->filepath);
				
				//$recording = "=HYPERLINK(\"{$recording->filepath}\", \"Click to listen\")"; // haven't figured out how to get this working in a CSV file
				$recording = "=HYPERLINK(\"{$archRec->filepath}\")";
				$recordingSize = $archRec->filesize;
				$recordingSecs = (int)((double)$recordingSize / 16384.0); // 16-bit WAV
 				$recordingTime = strftime('%T', $recordingSecs);
 				
 				$this->addAuditDetails($archRec, $startTimeUTC); 				
			}
			else if (is_double($archRec))
			{
				LogMessage("Recording result ID {$resID} (AUUID '{$AUUID}') was too big to export: {$archRec} bytes - skipping recording");
				
 				// export of this specific entry was unsuccessful due to the size of the recording
 				// (however we still 'may' be able to export other entries in zip if recording size OK or no recordings)			
				$recording = "\"File too big\"";
				$recordingSize = $archRec; // could be bigger than int max? leave as double
				$recordingSecs = (int)($recordingSize / 16384.0); // 16-bit WAV
 				$recordingTime = strftime('%T', $recordingSecs);
 				
 				$success = false;
			}
			else // not archived yet or already purged
			{
				if (!is_string($archRec) || empty($archRec))
					$archRec = 'Not Available';
		
				LogWarning("No recording for result ID {$resID}, AUUID {$AUUID}, reason '{$archRec}' - skipping recording", null, 'U:ARCX');
				$recording = "\"{$archRec}\"";
			}
		}
		
		$policy = $logData['Policy'];
		$from = $logData['From_User'];
		$fromDev = $logData['From_Dev'];
		$fromHangup = $logData['From_HangUp'];
		$toUser = $logData['To_User'];
		$toDev = $logData['To_Dev'];
		$toNumberDialled = $logData['To_NumberDialled'];
		$toHangup = $logData['To_HangUp'];
		$timeHuntingSecs = $logData['Time_Hunting'];
		$timeRingingSecs = $logData['Time_Ringing'];
		$timeTalkingSecs = $logData['Time_Talking'];
		$direction = $logData['Direction'];
		$AFlags = (int)$logData['AFlags'];
		$BFlags = (int)$logData['BFlags'];
		//$fromPolArchID = (int)$logData['From_PolArchID'];
		//$fromPnArchID = (int)$logData['From_PnArchID'];
		//$toPolArchID = (int)$logData['To_PolArchID'];
		//$toPnArchID = (int)$logData['To_PnArchID'];
		$type = $logData['Type'];

		$startTimeLocal = formatTime($time, $this->userTimezone, 'Y-m-d H:i:s');
		$endTimeLocal = formatTime($time+$timeHuntingSecs+$timeRingingSecs+$timeTalkingSecs, $this->userTimezone, 'Y-m-d H:i:s');
		
		$totalCallTimeSecs = $timeHuntingSecs+$timeRingingSecs+$timeTalkingSecs;
		$totalCallTime = strftime('%T', $totalCallTimeSecs);
		
		if ($timeHuntingSecs > 0)
			$timeHunting = strftime('%T', $timeHuntingSecs);
		else
			$timeHunting = 'none';
		
		if ($timeRingingSecs > 0)
			$timeRinging = strftime('%T', $timeRingingSecs);
		else
			$timeRinging = 'none';
		
		$timeTalking = strftime('%T', $timeTalkingSecs);
		
		$direction = $this->callDirectionAsString($direction);
		$fromHangup = $this->hangupCauseAsString($fromHangup);
		$toHangup = $this->hangupCauseAsString($toHangup);
		
		if (($AFlags > 0 && $AFlags != 1024) || ($BFlags > 0 && $BFlags != 1024))
			$flags='YES';
		else
			$flags='NO';
		
		$transfer 			= $this->isFlagSet(0, $AFlags, $BFlags);
		$attTransfer 		= $this->isFlagSet(1, $AFlags, $BFlags);
		$blindTransfer 		= $this->isFlagSet(2, $AFlags, $BFlags);
		$cliWithheld 		= $this->isFlagSet(3, $AFlags, $BFlags);
		$alert 				= $this->isFlagSet(4, $AFlags, $BFlags);
		$callPickedUp 		= $this->isFlagSet(5, $AFlags, $BFlags);
		$intercepted 		= $this->isFlagSet(6, $AFlags, $BFlags);
		$callQueue 			= $this->isFlagSet(7, $AFlags, $BFlags);
		$eavesdropAttached 	= $this->isFlagSet(8, $AFlags, $BFlags);
		$eavesdrop 			= $this->isFlagSet(9, $AFlags, $BFlags);
		$recorded 			= $this->isFlagSet(10, $AFlags, $BFlags);
		$lateDivert 		= $this->isFlagSet(11, $AFlags, $BFlags);
		$earlyDivert 		= $this->isFlagSet(12, $AFlags, $BFlags);
		$voiceMail 			= $this->isFlagSet(13, $AFlags, $BFlags);
		$diverted 			= $this->isFlagSet(14, $AFlags, $BFlags);
		$listenIn 			= $this->isFlagSet(15, $AFlags, $BFlags);
		$activeRecOK 		= $this->isFlagSet(16, $AFlags, $BFlags);
		$activeRecFail 		= $this->isFlagSet(17, $AFlags, $BFlags);
		$activeRecBufOK 	= $this->isFlagSet(18, $AFlags, $BFlags);
		$activeRecBufFail 	= $this->isFlagSet(19, $AFlags, $BFlags);
		$pcap 				= $this->isFlagSet(20, $AFlags, $BFlags);
		
		// don't think the following has ever worked in portal logs
		$fromType = '';
		$fromLabel = '';
		$toType = '';
		$toLabel = '';
		
		$csvEntryFields = array
		(
			$recording,			// Recording
			$recordingTime,		// Recording Time
			$recordingSize,		// Recording Size (Bytes)			
			$startTimeLocal,	// Date / Time
			$startTimeUTC,		// UTC Date / Time
			$endTimeLocal,		// End Date / Time
			$totalCallTime,		// Total Call Time
			$totalCallTimeSecs,	// Total Call Time (INT)
			$from,				// From
			$fromType,			// From Type
			$fromLabel,			// From Label
			$fromNumber,		// From (Number)
			$fromDev,			// From (Device)
			$toNumberDialled, 	// Dialled (Number)
			$toUser, 			// Connected To
			$toType, 			// To Type
			$toLabel, 			// To Label
			$toNumber, 			// Connected To (Number)
			$toDev, 			// Connected To (Device)
			$timeHunting,		// Time Hunting
			$timeRinging,		// Time Ringing
			$timeTalking,		// Time Talking
			$timeHuntingSecs,	// Time Hunting (INT)
			$timeRingingSecs,	// Time Ringing (INT)
			$timeTalkingSecs,	// Time Talking (INT)
			$direction,			// Direction
			$fromHangup,		// From HangUp
			$toHangup,			// To HangUp
			$type, 				// Type
			$flags,				// Flags
			$policy,			// Policy
			$AUUID,				// AUUID
			$BUUID,				// BUUID
			$transfer,			// Transfer
			$attTransfer,		// Att Transfer
			$blindTransfer,		// Blind Transfer
			$cliWithheld, 		// CLI Withheld
			$alert, 			// Alert
			$callPickedUp,		// Call Picked Up
			$intercepted,		// Intercepted
			$callQueue,			// Call Queue
			$eavesdropAttached,	// Eavesdrop Attached
			$eavesdrop,			// Eavesdrop
			$recorded,			// Recorded
			$earlyDivert,		// Early Divert
			$lateDivert,		// Late Divert
			$voiceMail,			// Voice Mail
			$diverted,			// Diverted
			$listenIn,			// Listen In,
			$activeRecOK, 		// Active Rec OK
			$activeRecFail,		// Active Rec Fail
			$activeRecBufOK, 	// Active Rec Buffered OK
			$activeRecBufFail,	// Active Rec Buffered Fail
			$pcap				// PCAP
		);
		
		foreach ($csvEntryFields as $index => &$csvEntryField)
		{
			if ($index != 0)
				// don't quote recording field as HYPERLINK macro won't work
				$csvEntryField = "\"{$csvEntryField}\"";
		}
		$csvEntryLine = implode(',', $csvEntryFields);
		
		if (fwrite($this->csvFile, $csvEntryLine."\r\n") === false)
			throw new Exception("Failed to write entry line to CSV file");
		
		return $success;
	}
	
	protected function processApiLogsData(SimpleXMLElement $logsResponseDom, array &$logsData)
	{
		$fields = array();
		$policies = array();
		$users = array();
		$devs = array();
		$numbers = array();
		$polArchs = array();
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Fields[1]/F', $fields);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Policies[1]/P', $policies);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Users[1]/U', $users);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Devs[1]/D', $devs);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Numbers[1]/N', $numbers);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/PolArchs[1]/P', $polArchs);
		
		if (count($fields) == 0)
			throw new Exception("Failed to fetch logs result for logs query QID {$this->queryQID}");
		
		$this->getCallLogsData($logsResponseDom, $fields, $users, $devs, $policies, $logsData);	
	}
	
	private function getCallLogsData(SimpleXMLElement $dom, array $fields, array $users, array $devs, array $policies, array &$data)
	{
		$data = array();
		
		$nodes = $dom->xpath('Data[1]/L');
		if (!is_array($nodes))
			return;
		
		foreach ($nodes as $node) 
		{
			// for each data row, decode column IDs into column names
			$dataRow = array();
			$resID = false;
			for ($index=0; $index<count($fields); ++$index)
			{
				$id = 'F'.$index;
				
				$fieldName = $fields[$id];
				
				$valueNode = $node->xpath('@'.$id);
				$value = (string)$valueNode[0];
				
				$dataRow[$fieldName] = $value;
				
				switch ($fieldName)
				{
					case 'ResID':
						$resID = (int)$value;
						break;
					case 'From_UserID':
						if (((int)$value > 0) && isset($users[$value]))
							$dataRow['From_User'] = $users[$value];
						else
							$dataRow['From_User'] = '';
						break;
					case 'To_UserID':
						if (((int)$value > 0) && isset($users[$value]))
							$dataRow['To_User'] = $users[$value];
						else
							$dataRow['To_User'] = '';
						break;
					case 'From_DevID':
						if (((int)$value > 0) && isset($devs[$value]))
							$dataRow['From_Dev'] = $devs[$value];
						else
							$dataRow['From_Dev'] = '';
						break;
					case 'To_DevID':
						if (((int)$value > 0) && isset($devs[$value]))
							$dataRow['To_Dev'] = $devs[$value];
						else
							$dataRow['To_Dev'] = '';
						break;
					case 'PolicyID':
						if (((int)$value > 0) && isset($policies[$value]))
							$dataRow['Policy'] = $policies[$value];
						else
							$dataRow['Policy'] = '';
						break;
					default:
						break;
				}
			}
			
			if ($resID !== false)
				$data[$resID] = $dataRow;
		}
	}	
	
	protected function getLogsAndRetentionDbResults()
	{
		return $this->app->bigDbc->query(
			"SELECT \n" .
			"LOWER(HEX(l.`AUUID`)) AS `HexAUUID`, \n" .
			"LOWER(HEX(l.`BUUID`)) AS `HexBUUID`, \n" .
			"l.*, \n" .
			"ra.`LegUUID` AS `RetentionALegUUID`, \n" .
			"ra.`UserID` AS `RetentionALegUserID`, \n" .
			"ra.`PolicyID` AS `RetentionALegPolicyID`, \n" .
			"ra.`PolicyVersion` AS `RetentionALegPolicyVersion`, \n" . 
			"ra.`State` AS `RetentionALegState`, \n" . 
			"ra.`Purged` AS `RetentionALegPurged`, \n" . 
			"rb.`LegUUID` AS `RetentionBLegUUID`, \n" .
			"rb.`UserID` AS `RetentionBLegUserID`, \n" .
			"rb.`PolicyID` AS `RetentionBLegPolicyID`, \n" .
			"rb.`PolicyVersion` AS `RetentionBLegPolicyVersion`, \n" . 
			"rb.`State` AS `RetentionBLegState`, \n" . 
			"rb.`Purged` AS `RetentionBLegPurged` \n" .
			"FROM `Org_{$this->orgID}`.`Results_Logs_{$this->userID}_{$this->queryQID}` l \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` ra ON (l.`AUUID`=ra.`LegUUID`) \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` rb ON (l.`BUUID`=rb.`LegUUID`)"
		)->fetch_all(MySQLiResult::AS_ASSOC);
	}
	
	private function getArchivedRecording(array $logData, array $res, $prefix)
	{
		$resID = $logData['ResID'];
		
		$uuid = false;
		$policyVersion = false;
		$policyID = false;
		$ownerUserID = false;
		$purged = false;

		if (!empty($res['RetentionALegUUID']))
		{
			if ($res['RetentionALegState'] == 'Available')
			{
				$purged = !($res['RetentionALegPurged'] == 'no');	
				if (!$purged) 
				{
					$uuid = $logData['AUUID'];
					$policyVersion = (int)$res['RetentionALegPolicyVersion'];
					$policyID = (int)$res['RetentionALegPolicyID'];
					$ownerUserID = (int)$res['RetentionALegUserID'];
				}
				else
				{
					LogWarning("Processing result ID {$resID}: call is recorded but retention entry for ALeg UUID '{$logData['AUUID']}' has been purged", null, 'U:ARCX');
				}
			}
			else
			{
				LogWarning("Processing result ID {$resID}: call is recorded but retention entry for ALeg UUID '{$logData['AUUID']}' not available: State: {$res['RetentionALegState']}, Purged: {$res['RetentionALegPurged']}", null, 'U:ARCX');
			}
		}
		else if (!empty($res['RetentionBLegUUID'])) 
		{
			if ($res['RetentionBLegState'] == 'Available')
			{
				$purged = !($res['RetentionBLegPurged'] == 'no');
				if (!$purged) 
				{
					$uuid = $logData['BUUID'];
					$policyVersion = (int)$res['RetentionBLegPolicyVersion'];
					$policyID = (int)$res['RetentionBLegPolicyID'];
					$ownerUserID = (int)$res['RetentionBLegUserID'];
				}
				else
				{
					LogWarning("Processing result ID {$resID}: call is recorded but retention entry for BLeg UUID '{$logData['BUUID']}' has been purged", null, 'U:ARCX');
				}				
			}
			else
			{
				LogWarning("Processing result ID {$resID}: call is recorded but retention entry for BLeg UUID '{$logData['BUUID']}' not available: State: {$res['RetentionBLegState']}, Purged: {$res['RetentionBLegPurged']}", null, 'U:ARCX');
			}
		}
		else
		{
			LogWarning("Processing result ID {$resID}: call is recorded but no retention entry found for ALeg UUID '{$logData['AUUID']}' or BLeg UUID '{$logData['BUUID']}'", null, 'U:ARCX');
		}
		
		if ($purged)
			return 'Purged';
		
		if (!$uuid)
			return 'Not Available';
					
		if (!$this->checkArchivingPolicyPermission($policyID, $ownerUserID))
			return 'Access Denied';
			
		// Construct storage key from UUID
		$storageKey = "recording/{$this->orgID}/{$uuid}";

		$content = null;
		$contentSize = $this->getS3ObjectContent($policyID, $policyVersion, $storageKey, $content);
		if (($contentSize > 0) && empty($content))
			// recording too big
			return $contentSize;
		
		$callrecordingsDir = "{$this->baseDir}/{$this->csvFileName}-recordings";
		$wavFileRelPath = "{$this->csvFileName}-recordings/{$prefix}-{$uuid}.wav";
		$wavFilePath = "{$this->baseDir}/{$wavFileRelPath}";
		$wavFile = null;
		
		try
		{
			if (!$this->createDirectoryTree($callrecordingsDir))
				throw new Exception("Failed to create temp directory '{$callrecordingsDir}'");
			
			$wavFile = fopen($wavFilePath, 'wb');
			if ($wavFile === false)
				throw new Exception ("Failed to create WAV file for writing: {$wavFilePath}");
			
			$wavFileSize = fwrite($wavFile, $content);
			if ($wavFileSize === false)
				throw new Exception ("Failed to to write to WAV file: {$wavFileSize}");
			
			LogMessage("Successfully wrote {$wavFileSize} bytes to {$wavFilePath}");
		}
		catch (Exception $e)
		{
			LogError($e);
			if ($wavFile !== null)
				fclose($wavFile);
			throw $e;
		}
		
		fclose($wavFile);
		
		$recording = new stdClass();
		$recording->filepath = $wavFileRelPath;
		$recording->filesize = $wavFileSize;
		$recording->uuid = $uuid;
		$recording->policyID = $policyID;
		
		return $recording;
	}
	
	private function isFlagSet($flag, $AFlags, $BFlags)
	{
		if (($AFlags & (1 << $flag)) || ($BFlags & (1 << $flag)))
			return 'YES';
		else
			return '-';	
	}
			
	private function hangupCauseAsString($cause)
	{
		switch ($cause)
		{
			case 'UNSPECIFIED':						return 'Unspecified error';
			case 'UNALLOCATED_NUMBER':
			case 'UNALLOCATED':						return 'Number unallocated';
			case 'NO_ROUTE_TRANSIT_NET':			return 'Unrecognised transit network';
			case 'NO_ROUTE_DESTINATION':			return 'No route destination';
			case 'CHANNEL_UNACCEPTABLE':			return 'Unacceptable channel';
			case 'CALL_AWARDED_DELIVERED':
			case 'NORMAL_CLEARING':					return 'Normal';
			case 'USER_BUSY':						return 'User busy';
			case 'NO_USER_RESPONSE':
			case 'NO_ANSWER':						return 'No answer';
			case 'SUSCRIBER_ABSENT':				return 'User disconnected';
			case 'CALL_REJECTED':					return 'Call rejected';
			case 'NUMBER_CHANGED':					return 'Unroutable call';
			case 'REDIRECTION_TO_NEW_DESTINATION':	return 'Call re-directed';
			case 'EXCHANGE_ROUTING_ERROR':			return 'Maximum number of call hops reached';
			case 'DESTINATION_OUT_OF_ORDER':		return 'Unable to connect';
			case 'INVALID_NUMBER_FORMAT':			return 'Invalid number format';
			case 'FACILITY_REJECTED':				return 'Destination unavailable';
			case 'RESPONSE_TO_STATUS_ENQUIRY':		return 'Response to status enquiry';
			case 'NORMAL_CIRCUIT_CONGESTION':		return 'Destination unavailable';
			case 'NETWORK_OUT_OF_ORDER':			return 'Network unavailable';
			case 'NORMAL_TEMPORARY_FAILURE':		return 'Temporary failure';
			case 'SWITCH_CONGESTION':				return 'Congestion';
			case 'ACCESS_INFO_DISCARDED':			return 'Access unavailable';
			case 'REQUESTED_CHAN_UNAVAIL':			return 'Destination channel unavailable';
			case 'FACILITY_NOT_SUSCRIBED':			return 'Unauthorised access';
			case 'OUTGOING_CALL_BARRED':
			case 'INCOMING_CALL_BARRED':			return 'Call barred';
			case 'BEARERCAPABILITY_NOTAUTH':		return 'Unauthorised access';
			case 'BEARERCAPABILITY_NOTAVAIL':		return 'Bearer unavailable';
			case 'SERVICE_UNAVAILABLE':				return 'Service unavailable';
			case 'BEARERCAPABILITY_NOTIMPL':		return 'Bearer not implemented';
			case 'CHAN_NOT_IMPLEMENTED':			return 'Channel not implemented';
			case 'FACILITY_NOT_IMPLEMENTED':		return 'Facility not implemtented';
			case 'SERVICE_NOT_IMPLEMENTED':			return 'Service not implemented';
			case 'INVALID_CALL_REFERENCE':			return 'Invalid call reference';
			case 'INCOMPATIBLE_DESTINATION':		return 'Incompatible destination';
			case 'ORIGINATOR_CANCEL':				return 'Originator cancelled';
			case 'BLIND_TRANSFER':					return 'Blind transfer';
			case 'ATTENDED_TRANSFER':				return 'Attended transfer';
			case 'ALLOTTED_TIMEOUT':				return 'Timeout exceeded';
			case 'USER_NOT_REGISTERED':				return 'User failed to register';
			case 'NO_ROUTE_TRANSIT_NET':			return 'Unallocated number';
			case 'NORMAL_UNSPECIFIED':				return 'Normal';
			case 'NO_ANSWER':						return 'No answer';
			case 'RECOVERY_ON_TIMER_EXPIRE':		return 'Device timeout';
			case 'LOSE_RACE':						return 'Another device answered';
			case 'SYSTEM_SHUTDOWN':					return 'System shutdown';
			default: return $cause;
		}
	}

	private function callDirectionAsString($direction)
	{
		switch ($direction)
		{
			case 'IN': 	return 'Inbound';
			case 'OUT':	return 'Outbound';
			case 'INT':	return 'Internal';
			case 'EXT':	return 'External';
			case 'SER':	return 'Service';
			case 'ERR':	return 'Error';
			default:	return 'N/A';
		}
	}	
		
	protected function getCsvHeaderFields()
	{
		return array
		(
			'Recording',
			'Recording Time',
			'Recording Size (Bytes)',
			'Date / Time',
			'UTC Date / Time',
			'End Date / Time',
			'Total Call Time',
			'Total Call Time (INT)',
			'From',
			'From Type',
			'From Label',
			'From (Number)',
			'From (Device)',
			'Dialled (Number)',
			'Connected To',
			'To Type',
			'To Label',
			'Connected To (Number)',
			'Connected To (Device)',
			'Time Hunting',
			'Time Ringing',
			'Time Talking',
			'Time Hunting (INT)',
			'Time Ringing (INT)',
			'Time Talking (INT)',
			'Direction',
			'From HangUp',
			'To HangUp',
			'Type',
			'Flags',
			'Policy',
			'AUUID',
			'BUUID',
			'Transfer',
			'Att Transfer',
			'Blind Transfer',
			'CLI Withheld',
			'Alert',
			'Call Picked Up',
			'Intercepted',
			'Call Queue',
			'Eavesdrop Attached',
			'Eavesdrop',
			'Recorded',
			'Early Divert',
			'Late Divert',
			'Voice Mail',
			'Diverted',
			'Listen In',
			'Active Rec OK',
			'Active Rec Fail',
			'Active Rec Buffered OK',
			'Active Rec Buffered Fail',
			'PCAP'
		);
	}
}

class ArchivingSMSExport extends ArchivingExport
{
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword, $reason)
	{
		parent::__construct($app, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword, $reason);
	}
	
	public function displayType() { return 'SMS/MMS'; }
	protected function archivingType() { return 'sms'; }
	
	protected function getApiLogsPath()
	{
		return '/smslogs/download';
	}
		
	protected function exportLogEntry(array &$logData, array &$res)
	{
		$UUID = $logData['UUID'];
		$user = $logData['User'];
		$time = (int)$logData['Time'];	
		$fromNumber = $logData['From_Number'];
		$toNumber  = $logData['To_Number'];
		$direction = $logData['Direction'];
		//$hostname = $logData['HostName']; // Not used
		$resID = (int)$res['ResID'];
		
		$timeUTC = gmstrftime('%F %T', $time);

		LogVerbose("Processing SMS/MMS logs result ID {$resID}, UUID: {$UUID}...");
		
		$success = true;
		
		$messageSize = '';
		
		$prefix = formatTime($time, $this->userTimezone, 'Y-m-d@H.i.s') . "-from-{$fromNumber}-to-{$toNumber}";
		$archMsg = $this->getArchivedMessage($logData, $res, $prefix);
		if (isset($archMsg->filepath))
		{
			LogVerbose("Fetched MMS for result ID {$resID}: {$archMsg->filepath}");		
						
			$this->addToZipArchive($archMsg->filepath);
			
			//$message = "=HYPERLINK(\"{$archMsg->filepath}\", \"Click to view\")"; // haven't figured out how to get this working in a CSV file
			$message = "=HYPERLINK(\"{$archMsg->filepath}\")";
			$messageSize = $archMsg->filesize;
			
			$this->addAuditDetails($archMsg, $timeUTC);
		}
		else if (isset($archMsg->text))
		{
			LogDebug("Fetched SMS for result ID {$resID}");
			
			$text = str_replace("\"", "\"\"", $archMsg->text);
			$message = "\"{$text}\"";
			$messageSize = strlen($archMsg->text);

			$this->addAuditDetails($archMsg, $timeUTC);
		}
		else if (is_double($archMsg))
		{
			LogMessage("SMS/MMS result ID {$resID} (UUID '{$UUID}') was too big to export: {$archMsg} bytes - skipping message");
			
			// export of this specific entry was unsuccessful due to the size of the media
			// (however we still 'may' be able to export other entries in zip if media size OK or no media)			
			$message = "\"File too big\"";
			$messageSize = $archMsg; // could be bigger than int max? leave as double
			
			$success = false;
		}		
		else
		{
			if (!is_string($archMsg) || empty($archMsg))
				$archMsg = 'Not Available';
	
			LogWarning("No SMS/MMS message for result ID {$resID}, AUUID {$AUUID}, reason '{$archMsg}' - skipping message", null, 'U:ARCX');
			$message = "\"{$archMsg}\"";		
		}
						
		$timeLocal = formatTime($time, $this->userTimezone, 'Y-m-d H:i:s');
								
		$csvEntryFields = array
		(
			$message,		// Message
			$messageSize,	// Message Size
			$timeLocal,		// Date / Time
			$timeUTC,		// UTC Date / Time
			$user,			// User
			$fromNumber,	// From (Number)
			$toNumber, 		// To (Number)
			$direction,		// Direction
			$UUID,			// UUID
		);
		
		foreach ($csvEntryFields as $index => &$csvEntryField)
		{
			if ($index != 0)
				// don't quote message field as HYPERLINK macro won't work
				$csvEntryField = "\"{$csvEntryField}\"";
		}
		$csvEntryLine = implode(',', $csvEntryFields);
		
		if (fwrite($this->csvFile, $csvEntryLine."\r\n") === false)
			throw new Exception("Failed to write entry line to CSV file");
		
		return $success;
	}
	
	private function getArchivedMessage(array $logData, array $res, $prefix)
	{
		$resID = $logData['ResID'];
		
		$uuid = false;
		$policyVersion = false;
		$policyID = false;
		$ownerUserID = false;
		$purged = false;

		if (!empty($res['RetentionUUID']))
		{
			if ($res['RetentionState'] == 'Available') 
			{
				$purged = !($res['RetentionPurged'] == 'no');
				if (!$purged) 
				{
					$uuid = $logData['UUID'];
					$policyVersion = (int)$res['RetentionPolicyVersion'];
					$policyID = (int)$res['RetentionPolicyID'];
					$ownerUserID = (int)$res['RetentionUserID'];
				}
				else
				{
					LogWarning("Processing result ID {$resID}: retention entry for UUID '{$logData['UUID']}' is purged", null, 'U:ARCX');
				}
			}
			else
			{
				LogWarning("Processing result ID {$resID}: retention entry for UUID '{$logData['UUID']}' not available: State: {$res['RetentionState']}, Purged: {$res['RetentionPurged']}", null, 'U:ARCX');
			}
		}
		else
		{
			LogWarning("Processing result ID {$resID}: no retention entry found for UUID '{$logData['UUID']}'", null, 'U:ARCX');
		}
		
		if ($purged)
			return 'Purged';
		
		if (!$uuid)
			return 'Not Available';
					
		if (!$this->checkArchivingPolicyPermission($policyID, $ownerUserID))
			return 'Access Denied';

		// Construct storage key from UUID
		$storageKey = "sms/{$this->orgID}/{$uuid}";
			
		// a big MMS probably already contains compressed media (mp4 etc), so we don't expect to get further compression 
		// - hence specifying expected p7zip compression ratio of 1.0 (i.e. no compression)
		$content = null;
		$contentSize = $this->getS3ObjectContent($policyID, $policyVersion, $storageKey, $content);
		if (($contentSize > 0) && empty($content))
			// recording too big
			return $contentSize;						
		
		$message = new stdClass();
		$message->uuid = $uuid;
		$message->policyID = $policyID;

		// seems crude, but this is exactly how the portal determines if message is SMS or MMS
		$xmlPos = strpos($content, "<?xml");
		if (($xmlPos === 0) || ($xmlPos === 1)) 
		{
			// SMS message - extract text field from XML
			
			//LogDebug("SMS XML: {$content}");
			
			$smsDom = simplexml_load_string($content);
			if (!($smsDom instanceof SimpleXMLElement)) 
				throw new Exception("Failed to parse SMS XML details");
			
			$textNode = $smsDom->xpath('Text[1]');
			if (empty($textNode)) 
				throw new Exception("Failed to extract text from SMS XML details");
			$text = (string)$textNode[0];			
			
			$message->text = html_entity_decode($text, ENT_COMPAT, 'UTF-8');
		} 
		else
		{
			// MMS message - write contents to .eml file
			
			$mmsDir = "{$this->baseDir}/{$this->csvFileName}-mms";
			$mmsFileRelPath = "{$this->csvFileName}-mms/{$prefix}-{$uuid}.eml";
			$mmsFilePath = "{$this->baseDir}/{$mmsFileRelPath}";
			$mmsFile = null;
			
			try
			{
				if (!$this->createDirectoryTree($mmsDir))
					throw new Exception("Failed to create temp directory '{$mmsDir}'");
				
				$mmsFile = fopen($mmsFilePath, 'wb');
				if ($mmsFile === false)
					throw new Exception ("Failed to create MMS file for writing: {$mmsFilePath}");
				
				$mmsFileSize = fwrite($mmsFile, $content);
				if ($mmsFileSize === false)
					throw new Exception ("Failed to to write to MMS file: {$mmsFilePath}");
				
				LogMessage("Successfully wrote {$mmsFileSize} bytes to {$mmsFilePath}");
			}
			catch (Exception $e)
			{
				LogError($e);
				if ($mmsFile !== null)
					fclose($mmsFile);
				throw $e;
			}
			
			fclose($mmsFile);
			
			$message->filepath = $mmsFileRelPath;
			$message->filesize = $mmsFileSize; 
		}
		return $message;
	}	
	
	protected function processApiLogsData(SimpleXMLElement $logsResponseDom, array &$logsData)
	{
		$fields = array();
		$users = array();
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Fields[1]/F', $fields);
		$this->getLogsIDMap($logsResponseDom, 'Maps[1]/Users[1]/U', $users);
		
		if (count($fields) == 0)
			throw new Exception("Failed to fetch logs result for logs query QID {$this->queryQID}");
		
		$this->getSMSLogsData($logsResponseDom, $fields, $users, $logsData);	
	}
	
	private function getSMSLogsData(SimpleXMLElement $dom, array $fields, array $users, array &$data)
	{
		$data = array();
		
		$nodes = $dom->xpath('Data[1]/L');
		if (!is_array($nodes))
			return;
		
		foreach ($nodes as $node) 
		{
			// for each data row, decode column IDs into column names
			$dataRow = array();
			$resID = false;
			for ($index=0; $index<count($fields); ++$index)
			{
				$id = 'F'.$index;
				
				$fieldName = $fields[$id];
				
				$valueNode = $node->xpath('@'.$id);
				$value = (string)$valueNode[0];
				
				$dataRow[$fieldName] = $value;
				
				switch ($fieldName)
				{
					case 'ResID':
						$resID = (int)$value;
						break;
					case 'UserID':
						if (((int)$value > 0) && isset($users[$value]))
							$dataRow['User'] = $users[$value];
						else
							$dataRow['User'] = '';
						break;
					default:
						break;
				}
			}
			
			if ($resID !== false)
				$data[$resID] = $dataRow;
		}
	}
	
	protected function getLogsAndRetentionDbResults()
	{
		return $this->app->bigDbc->query(
			"SELECT \n" .
			"LOWER(HEX(l.`UUID`)) AS `HexUUID`, \n" .
			"l.*, \n" .
			"r.`UUID` AS `RetentionUUID`, \n" .
			"r.`UserID` AS `RetentionUserID`, \n" .
			"r.`PolicyID` AS `RetentionPolicyID`, \n" .
			"r.`PolicyVersion` AS `RetentionPolicyVersion`, \n" . 
			"r.`State` AS `RetentionState`, \n" . 
			"r.`Purged` AS `RetentionPurged` \n" . 
			"FROM `Org_{$this->orgID}`.`Results_SMSLogs_{$this->userID}_{$this->queryQID}` l \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` r ON (l.`UUID`=r.`UUID`)"
		)->fetch_all(MySQLiResult::AS_ASSOC);
	}
	
	protected function getCsvHeaderFields()
	{
		return array
		(
			'Message',
			'Message Size (Bytes)',
			'Date / Time',
			'UTC Date / Time',
			'User',
			'From (Number)',
			'To (Number)',
			'Direction',
			'UUID',		
		);
	}	
}

class ArchivingExportApp extends BaseApp
{
	const CONFIG_PATH = '/etc/archiving/export.conf';
	
	public $api = null;

	public $coreDbc = null;
	public $bigDbc = null;
	
	public $hAPISearchDomain = null;
	private $hAPILocName = null;
	
	private $passwordAESKey = null;
	
	public $maxZipFileSizeMB = null;
	public $maxZipFileCount = null;
	
	public $cliMode = false;
	
	public $maxRetries = 2;

	/**
	 * run
	 *
	 * @throws Exception
	 *
	 * @return integer
	 */
	public function run()
	{
		$hAPIHost = new HAPIHost();
		$this->hAPISearchDomain = $hAPIHost->get('SearchDomain');
		$this->hAPILocName = $hAPIHost->get('LocName');

		$authCreds = $this->conf->get('coreapi.authorization_creds', false);
		if (empty($authCreds))
			throw new Exception('CoreAPI auth creds not configured');
		
		if ($hAPIHost->InProduction())
			$this->passwordAESKey = $this->conf->get('coreapi.logsexportq.aeskey', false);	
		else
			$this->passwordAESKey = 'UNSECURE';
		if (empty($this->passwordAESKey))
			throw new Exception('LogExportQ AESKey not configured');
		
		$this->passwordAESKey = sha1($this->passwordAESKey);
		
		$this->maxFileSizeMB = (double)$this->conf->get('export.maxfilesizemb', 3000.0);
		$this->maxZipFileSizeMB = (double)$this->conf->get('export.maxzipfilesizemb', 500.0);
		$this->maxZipFileCount = (int)$this->conf->get('export.maxzipfilecount', 20);
		$this->maxRetries = (int)$this->conf->get('export.maxretries', 2);
		
		$coreapi = $this->conf->get('coreapi.url', 'http://api.{hapi:SearchDomain}');
		if (empty($coreapi))
			throw new Exception('CoreAPI URL not configured');
			
		$this->cliMode = cli::get('--no-daemon', false);
		
		$authorizationSentry = null;
		$requests = array();
		try
		{			
			// Get Big SQL database connection
			$this->bigDbc = MySQLiConnection::Get('retention_rw');
			
			$queryQID = 0;
			if ($this->cliMode)
			{
				// can be run from command line to export a specific call/SMS logs result set
				$queryQID = cli::get('--queryqid', false);
			}
				
			if ($queryQID == 0) 
			{			
				$requests = $this->getPendingExportRequests();
				if (empty($requests)) 
				{
					LogVerbose("No pending archiving export requests to service");	
					return 0;
				}
			}
			
			API::Init($coreapi, new ArrayStorage(), APP_NAME_.':1.0');
			
			API::LoginBasicAuth($authCreds, null/*mfa*/, true/*re_authenticate*/);
			
			LogDebug("Got auth CoreAPI token: ".API::GetToken());
			
			$this->api = array($coreapi, API::GetToken());
			/** @noinspection PhpUnusedLocalVariableInspection */
			
			EventLog::Configure($coreapi);
			EventLog::SetAuthorization(API::GetToken());

			$authorizationSentry = new ScopeSentry(
				function()
				{
					//unset($this->api);
					API::LogOut();
				}
			);
			
			// Get CoreAPI SQL connection
			$this->coreDbc = MySQLiConnection::Get('coresql');	
		}
		catch (Exception $e)
		{
			LogWarning("Failed to run archiving exporter: Exception: {$e}");	
			return 1;
		}
		
		if ($queryQID > 0)
		{	
			// command line export of specific call/SMS logs result set
			$orgID = cli::get('--orgid', 0);
			if ($orgID == 0)
				throw new Exception('No org ID specified');
								
			$userID = cli::get('--userid', 0);
			if ($userID == 0)
				throw new Exception('No user ID specified');
				
			$type = cli::get('--type', '');
			if (empty($type))
				throw new Exception('No type specified');
			$displayType = ($type == 'SMS' ? 'SMS/MMS' : $type);	
			
			$resultIDs = array();
			$resIDs = explode(',', cli::get('--resultids', ''));
			foreach ($resIDs as $id)
			{
				$id = (int)$id;
				if ($id > 0)
					$resultIDs[$id] = $id;
			}
							
			$zipPassword = cli::get('--password', 'letmein');
			$exportID = cli::get('--exportid', -1);
			
			$export = null;
			try
			{
				if ($type != 'Call' && $type !='SMS')
					throw new Exception("Unhandled export type '{$type}'");			
					
				if ($exportID > 0)
					$this->updateExportStatus($exportID, 'Processing');
															
				$className = "Archiving{$type}Export";
				$export = new $className($this, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword);
				$result = $export->generate();
				
				LogVerbose("Generated export OK with URI '{$result->exportURI}'");
								
				if ($exportID > 0)
				{
					$this->updateExportStatus($exportID, 'Complete', $result);	
				}		
			}
			catch (Exception $e)
			{
				if ($exportID > 0) 
					$status = $this->updateExportStatus($exportID, 'Failed', 1/*$serviceFailedCount*/);
				else
					$status = 'Failed';
				
				if ($status == 'Failed')
				{
					LogWarning("Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: Exception: {$e}", null, 'U:ARCX');
					if (is_object($export))
						$export->writeEventLogFailed();
				}
				else // we will retry in a bit
				{
					LogMessage("Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID} - retrying again in a bit... (Exception: {$e})");
				}
			}
			unset($export);
		}
		else // daemon mode - we have a pending request(s)
		{					
			foreach ($requests as $request)
			{
				$exportID = (int)$request['ExportID'];
				$orgID = (int)$request['OrgID'];
				$userID = (int)$request['UserID'];
				$type = $request['Type'];
				$displayType = ($type == 'SMS' ? 'SMS/MMS' : $type);
				$queryQID = (int)$request['QueryQID'];
				$resultIDs = array();
				$resIDs = explode(',', $request['ResultIDs']);
				$reason = $request['ReasonForAccess'];
				
				$serviceFailedCount = (int)$request['ServiceFailedCount'];
				foreach ($resIDs as $id)
				{
					$id = (int)$id;
					// there is a weird bug in portal which means it sometimes sets duplicate result IDs - de-duplicate here
					if (($id > 0) && !isset($resultIDs[$id]))
						$resultIDs[$id] = $id;
				}
				$zipPassword = $request['Password'];
				
				$export = null;
				try
				{
					if ($type != 'Call' && $type !='SMS')
						throw new Exception("Unhandled export type '{$type}'");
					
					$this->updateExportStatus($exportID, 'Processing');
					
					$className = "Archiving{$type}Export";
					$export = new $className($this, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword, $reason);
					$result = $export->generate();
							
					LogMessage("Successfully exported {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: ExportURI: {$result->exportURI}");
										
					$this->updateExportStatus($exportID, 'Complete', $result);
				}
				catch (Exception $e)
				{
					$status = $this->updateExportStatus($exportID, 'Failed', $serviceFailedCount);
					if ($status == 'Failed')
					{
						LogWarning("Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: Exception: {$e}", null, 'U:ARCX');
						if (is_object($export))
							$export->writeEventLogFailed();
					}
					else // we will retry in a bit
					{
						LogMessage("Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID} - retrying again in a bit... (Exception: {$e})");
					}
				}
				unset($export);
			}
		}
		
		return 0;
	}
	
	private function getPendingExportRequests()
	{
		$this->cleanupExportStatus();
		
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
						
		$result = $this->bigDbc->query(
			"SELECT `ExportID`, ".
			"`Submitted`, ".
			"`OrgID`, ".
			"`UserID`, ".
			"`Type`, ".
			"`QueryQID`, ".
			"`ResultIDs`, ".
			"AES_DECRYPT(`EncPwd`, CONCAT(`ExportID`, `OrgID`, '{$this->passwordAESKey}', `QueryQID`)) AS `Password`, ".
			"`ReasonForAccess`, ".
			"`ServiceFailedCount` ".
			"FROM `Aux`.`LogsExportQ` ".
			"WHERE Status='Pending' ". 
			"AND `ClientLocation`='{$locName}' ".
			"AND (`NextServiceAttempt` IS NULL OR UTC_TIMESTAMP() >= `NextServiceAttempt`) ".
			"ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT 100;"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		return $result;	
	}
		
	private function cleanupExportStatus()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		$this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='Pending', ".
			"`Result`='' ".
			"WHERE `Status`='Processing' ".
			"AND `ClientLocation`='{$locName}';"
		);
	}
	
	public function updateExportStatus($exportID, $status, $result=null)
	{
		static $startTime; 
		$additionalFields = '';
		
		switch ($status)
		{
			case 'Processing':
				$startTime = microtime(true/*asFloat*/);
				$additionalFields = ", `LastServiceAttempt`=UTC_TIMESTAMP(), `NextServiceAttempt`=NULL";
				break;
				
			case 'Progress':
				$result = $this->bigDbc->escape_string($result);
				$additionalFields .= ", `Result`='{$result}'";
				$status = 'Processing';
				break;
				
			case 'Complete':
				$additionalFields .= ", `EncPwd`=NULL";
				if (is_object($result))
				{
					if (isset($result->result) && (strlen($result->result) > 0))
					{
						$result->result = $this->bigDbc->escape_string($result->result);
						$additionalFields .= ", `Result`='{$result->result}'";
					}
					if (isset($result->policyID))
					{
						$additionalFields .= ", `PolicyID`='{$result->policyID}'";
					}	
					if (isset($result->exportURI))
					{
						$result->exportURI = $this->bigDbc->escape_string($result->exportURI);
						$additionalFields .= ", `ExportURI`='{$result->exportURI}'";
					}
				}
				
				$elapsedMSecs = (int)((microtime(true/*asFloat*/) - $startTime) * 1000.0);
				$additionalFields .= ", `DurationMS`={$elapsedMSecs}";
				
				$additionalFields .= ", `ServiceFailedCount`=NULL";
				break;
				
			case 'Failed':
				$elapsedMSecs = (int)((microtime(true/*asFloat*/) - $startTime) * 1000.0);
				$additionalFields .= ", `Result`='', `DurationMS`={$elapsedMSecs}";
				
				++$result;
				$additionalFields .= ", `ServiceFailedCount`={$result}";
				
				$retrySeconds = false;
				switch ($result) 
				{
					case 1:
						// first retry in 60 seconds
						if ($result <= $this->maxRetries)
							$retrySeconds = 60;
						break;
					
					case 2:			
					default:
						// second retry in 5 minutes
						if ($result <= $this->maxRetries)
							$retrySeconds = 300;
						break;					
				}
				
				if ($retrySeconds)
				{
					// set status back to 'Pending' and try again in a bit
					$status = 'Pending';
					$additionalFields .= ", `NextServiceAttempt`=DATE_ADD(UTC_TIMESTAMP(), INTERVAL {$retrySeconds} SECOND)";	
				}
				else
				{
					// set status to 'Failed' - no more retries
					$additionalFields .= ", `EncPwd`=NULL";
				}
							
				break;
				
			default:
				$additionalFields = '';
				break;
		}
		
		$escStatus = $this->bigDbc->escape_string($status);
		
		$this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='{$escStatus}'".
			$additionalFields .
			" WHERE `ExportID`={$exportID};"
		);
		
		return $status;
	}
}

/**
 * Show usage help, optionally with error message
 *
 * @param string $error error
 *
 * @return void
 */
function Usage($error = null)
{
	$self = basename(__FILE__, '.php');
	if(!is_null($error))
		$error = "\n\nERROR: $error\n\n";
	else
		$error = '';

	echo <<<HELP

Archiving Export$error
(C) Red Matter Ltd. UK

Usage: $self [--conf=/path/to/run.conf] [--no-daemon]

HELP;
}

/*
 * Terminate callback
 *
 * @param int $signo signal number
 *
 * @return void
 */
$terminateCallback = function($signo)
{
	LogVerbose("Received SIG:$signo - Terminating");
	DaemonScripts::Terminate($signo);
};

try
{
	cli::init(null, array('auth-creds:', 'auth-hash:', 'conf:', 'help', 'no-daemon', 'queryqid:', 'orgid:', 'userid:', 'type:', 'resultids:', 'password:', 'exportid:'));
	
	if (true === cli::get('help', false))
	{
		Usage();
		exit(0);
	}
	
	$app = ArchivingExportApp::getInstance(cli::get('--conf', ArchivingExportApp::CONFIG_PATH));	
	$conf = $app->getConf();
	
	// Biggest zip file size we allow is 500Mb by default, but a media file uncompressed could be much bigger than this - we allow up to 3G by default 
	// (and so in copying buffers during decryption we might have up 2X3G sitting in buffers)
	// Note 8G limit is tuned for production cdr boxes so we don't steal more than half the physical memory (16G) 
	$memoryLimit = $conf->get('app.memory_limit', '8G');
	ini_set('memory_limit', $memoryLimit);
		
	if (false === cli::get('--no-daemon', false))
	{
		DaemonScripts::Initialise(
			array(
				'runasUid' => $conf->get('app.run_as_user', 'cdrmunch'),
				'runasGid' => $conf->get('app.run_as_group', 'cdrmunch'),
				'pidfilePath' => $conf->get('app.pid_file', '/var/run/archiving/rmarchexport.pid'),
				'signalHandlers' => array(SIGTERM => $terminateCallback, SIGINT => $terminateCallback),
				'sleepInterval' => array(1 /*seconds and*/, 0 /* micro second */),
				'runnableFunction' => array($app, 'run')
			)
		);
	}
	else
	{
		// run once!!
		exit($app->run());
	}
}
catch (CLIUsageErrorException $e)
{
	Usage($e->getMessage());
	exit(1);
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}

