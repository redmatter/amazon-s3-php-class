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
define('RM_CLI_APP', true);

require_once 'rmapp/class.BaseApp.php';
require_once 'rmdaemonscripts/DaemonScripts.php';
require_once 's3/S3.php';

require_once 'restclient/RestClient.class.php';
require_once 'hPHP/hapi.php';

abstract class ArchivingExport
{
	protected $app = null;
	protected $exportID = null;
	protected $orgID = null;
	protected $userID = null;
	protected $queryQID = null;
	protected $resultIDs = null;
	protected $zipPassword = null;
	
	protected $encryptionKey = null;
	protected $policyID = null;
	protected $timezone = null;
	
	protected $policyPermissions = array();
	
	protected $baseDir = null;
	protected $csvFileName = null;
	protected $csvFilePath = null;
	protected $csvFile = false;
	protected $zipFileName = null;
		
	protected $endpointDetails = array();
	protected $endpoints = array();
	
	const TEMP_DIR = '/tmp/rmarchexport';
		
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword)
	{
		$this->app = $app;
		$this->exportID = (int)$exportID;
		$this->orgID = (int)$orgID;
		$this->userID = (int)$userID;
		$this->queryQID = (int)$queryQID;
		$this->resultIDs = $resultIDs;
		$this->zipPassword = (string)$zipPassword;
		
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
		
		//if (is_dir(self::TEMP_DIR))
		//	$this->cleanDirectory(self::TEMP_DIR);
	}
	
	abstract public function type();
	abstract protected function archivingType();
	
	public function generate()
	{
		$this->encryptionKey = null;
		$this->policyID = null;
		$this->timezone = null;
		
		$this->policyPermissions = array();
	
		$this->baseDir = null;
		$this->csvFileName = null;
		$this->csvFilePath = null;
		$this->csvFile = false;
		$this->zipFileName = null;
		
		$this->endpointDetails = array();
		$this->endpoints = array();
					
		LogMessage("Starting ".$this->type()." log export: ExportID: {$this->exportID}, OrgID: {$this->orgID}, UserID: {$this->userID}, QueryQID: {$this->queryQID}");
		LogMessage("Password: {$this->zipPassword}");
		
		$result = 'OK';
		
		// Get info/settings for this user/org
		
		$this->encryptionKey = $this->getOrgEncryptionKey($this->orgID);
		$this->policyID = $this->getOrgDefaultPolicyID($this->orgID);
		$this->timezone = $this->getUserTimeZone($this->userID);
				
		// OK and now the actual call logs

		$searchStartTime = false;
		$searchEndTime = false;
		$callLogsData = array();
		$this->getApiLogsData($searchStartTime, $searchEndTime, $callLogsData);

		$searchStartTime = $this->formatTime($searchStartTime, $this->timezone, 'Y-m-d@H.i.s');
		$searchEndTime = $this->formatTime($searchEndTime, $this->timezone, 'Y-m-d@H.i.s');

		//echo "logsData:\n".print_r($logsData);

		$dbRetResults = $this->getLogsAndRetentionDbResults();

		LogMessage("Processing ".count($dbRetResults)." ".$this->type()." log entries...");

		if (is_dir(self::TEMP_DIR))
			$this->cleanDirectory(self::TEMP_DIR);	
		$this->baseDir = self::TEMP_DIR."/{$this->exportID}";
		
		// ensure directory is removed first
		if (!$this->createDirectoryTree($this->baseDir))
			throw new Exception("Failed to create temp directory '{$this->baseDir}'");
		$exportUUID = UUID::v4();
		$this->csvFileName = $this->type()."-export-{$searchStartTime}-to-{$searchEndTime}-{$exportUUID}";
		$this->csvFilePath = "{$this->baseDir}/{$this->csvFileName}.csv";
		$this->zipFileName = "{$this->csvFileName}.zip";

		$this->csvFile = fopen($this->csvFilePath, 'w');
		if ($this->csvFile === false)
			throw new Exception("Failed to open CSV file for writing '{$this->csvFilePath}'");

		$csvHeaderLine = implode(',', $this->getCsvHeaderFields());
		if (fwrite($this->csvFile, $csvHeaderLine."\r\n") === false)
			throw new Exception("Failed to write header to CSV file '{$this->csvFilePath}'");

		foreach ($dbRetResults as $dbRetResult)
		{
			$resID = (int)$dbRetResult['ResID'];
			if (empty($resultIDs) || isset($resultIDs[$resID]))
			{	
				$callLogData = &$callLogsData[$resID];
				$success = $this->exportLogEntry($callLogData, $dbRetResult);
				if ($success == false)
				{
					// this will because we have reached zip file size limit
					$result = 'OK (truncated)';
					break;
				}
			}
		}

		fclose($this->csvFile);
		$this->csvFile = false;
		
		// finally add the csv file to the archive
		$this->addToZipArchive("{$this->csvFileName}.csv");

		// Upload zip to S3
		$zipFilePath = "{$this->baseDir}/{$this->zipFileName}";
		$exportURI = "export/{$this->orgID}/{$exportUUID}";
		$this->uploadZipArchive($zipFilePath, $exportURI);
		
		$res = new stdClass();
		$res->policyID = $this->policyID;
		$res->exportURI = $exportURI;
		$res->result = $result;
		
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
		
		$profilePesponse = trim($client->getResponse());
		if (empty($profilePesponse))
			throw new Exception("Failed to fetch user profile details for user ID {$userID}");
			
		if ($client->getResponseCode() != 200)	
			throw new Exception("Failed to fetch user profile details for user ID {$userID}, response: {$profilePesponse}");
		
		//echo "profilePesponse:\n{$profilePesponse}\n";

		$profileResponseDom = simplexml_load_string($profilePesponse);
		if (!($profileResponseDom instanceof SimpleXMLElement)) 
			throw new Exception("Failed to fetch user profile details for user ID {$userID}, response: {$profilePesponse}");
		
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
		
		echo "logsResponse:\n{$logsResponse}\n";
		
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
		if (empty($permsResponse))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}");
		
		if ($client->getResponseCode() != 200)
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
		
		$permsResponseDom = simplexml_load_string($permsResponse);
		if (!($permsResponseDom instanceof SimpleXMLElement))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
			
		$nodes = $permsResponseDom->xpath('@Allowed');
		if (empty($nodes))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
			
		LogDebug("Checked CoreAPI archiving policy permission for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");	
		
		$allowed = (string)$nodes[0];
		$allowed = ($allowed == 'YES');
		
		// cache result
		$this->policyPermissions[$policyID][$ownerUserID] = $allowed;	
		return $allowed;
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
		
		LogVerbose("Adding file {$relFileSpec} to export zip '{$this->zipFileName}' with command: {$cmd} -p********");
		
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
		
		LogDebug("Added file {$relFileSpec} to export zip '{$this->zipFileName}' OK");
	}

	protected function uploadZipArchive($zipFilePath, $storageKey)
	{
		$bucket = $this->getEndpointDetails($this->policyID, 0/*policyVersion*/)->bucket;
		$s3 = $this->getS3Endpoint($this->policyID, 0/*policyVersion*/);		
		
		LogVerbose("Uploading export zip {$storageKey} to bucket {$bucket} in S3...");
		
		$headers = array();
		if (!$s3->putObjectFile($zipFilePath, $bucket, $storageKey, S3::ACL_PRIVATE, $headers, 'binary/octet-stream'))
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

	protected function formatTime($time, $timeZone, $format)
	{
		$dt = new DateTime();
		$dt->setTimezone($timeZone);
		$dt->setTimestamp($time);
		return $dt->format($format);
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
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword)
	{
		parent::__construct($app, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword);
	}
	
	public function type() { return 'call'; }
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
		
		LogVerbose("Processing logs result ID {$resID}, AUUID: {$AUUID}, Recorded: '{$recorded}'...");
		
		$recording = '"None"';
		if ($recorded == 'YES')
		{
			$prefix = $this->formatTime($time, $this->timezone, 'Y-m-d@H.i.s') . "-from-{$fromNumber}-to-{$toNumber}";
			$recording = $this->getArchivedRecording($logData, $res, $prefix);
			if (strlen($recording) > 0)
			{
				LogVerbose("Fetched recording for result ID {$resID}: {$recording}");
				
				$zipFilePath = "{$this->baseDir}/{$this->csvFileName}.zip";
				if (file_exists($zipFilePath))
					$zipFilesize = (int)filesize($zipFilePath);
				else
					$zipFilesize = 0;
				
				$recordingFilesize = filesize("{$this->baseDir}/{$recording}");
				if (($recordingFilesize > 0) && (($zipFilesize+$recordingFilesize) >= ($this->app->zipFilesizeLimitMB * (1024.0*1024.0))))
				{
					LogWarning("ZIP archive would exceed {$app->this->zipFilesizeLimitMB} MB file size limit - truncating export zip before result ID {$resID}", null, 'U:ARCX');
					return false;
				}
				
				$this->addToZipArchive($recording);
				
				//$recording = "=HYPERLINK(\"{$recording}\", \"Click to listen\")"; // haven't figured out how to get this working in a CSV file
				$recording = "=HYPERLINK(\"{$recording}\")";
			}
			else if ($recording === false)
			{
				LogWarning("No authorisation to recording for result ID {$resID}, AUUID {$AUUID} - skipping recording file", null, 'U:ARCX');
				$recording = '"Access Denied"'; // user doesn't have permission to access file
			}
			else // if ($recording === null)
			{
				LogWarning("Recording for result ID {$resID}, AUUID {$AUUID} not available (hasn't been archived yet?)", null, 'U:ARCX');
				$recording = '"Not Available"'; // i.e. hasn't been archived yet
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

		$startTimeLocal = $this->formatTime($time, $this->timezone, 'Y-m-d H:i:s');
		$startTimeUTC = gmstrftime('%F %T', $time);
		$endTimeLocal = $this->formatTime($time+$timeHuntingSecs+$timeRingingSecs+$timeTalkingSecs, $this->timezone, 'Y-m-d H:i:s');
		
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
		
		// OK
		return true;
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
						$value = (int)$value;
						if ($value > 0)
							$dataRow['From_User'] = $users[$value];
						else
							$dataRow['From_User'] = '';
						break;
					case 'To_UserID':
						$value = (int)$value;
						if ($value > 0)
							$dataRow['To_User'] = $users[$value];
						else
							$dataRow['To_User'] = '';
						break;
					case 'From_DevID':
						$value = (int)$value;
						if ($value > 0)
							$dataRow['From_Dev'] = $devs[$value];
						else
							$dataRow['From_Dev'] = '';
						break;
					case 'To_DevID':
						$value = (int)$value;
						if ($value > 0)
							$dataRow['To_Dev'] = $devs[$value];
						else
							$dataRow['To_Dev'] = '';
						break;
					case 'PolicyID':
						$value = (int)$value;
						if ($value > 0)
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

		if (!empty($res['RetentionALegUUID']))
		{
			if ($res['RetentionALegState'] == 'Available' && $res['RetentionALegPurged'] == 'no') 
			{
				$uuid = $logData['AUUID'];
				$policyVersion = $res['RetentionALegPolicyVersion'];
				$policyID = (int)$res['RetentionALegPolicyID'];
				$ownerUserID = (int)$res['RetentionALegUserID'];
			}
			else
			{
				LogUWarning("Processing result ID {$resID}: call is recorded but retention entry for ALeg UUID '{$logData['AUUID']}' not available: State: {$res['RetentionALegState']}, Purged: {$res['RetentionALegPurged']}", null, 'U:ARCX');
			}
		}
		else if (!empty($res['RetentionBLegUUID'])) 
		{
			if ($res['RetentionBLegState'] == 'Available' && $res['RetentionBLegPurged'] == 'no') 
			{
				$uuid = $logData['BUUID'];
				$policyVersion = $res['RetentionBLegPolicyVersion'];
				$policyID = (int)$res['RetentionBLegPolicyID'];
				$ownerUserID = (int)$res['RetentionBLegUserID'];
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
		
		if (!$uuid)
		{
			// recording not available
			return null;
		}
					
		if (!$this->checkArchivingPolicyPermission($policyID, $ownerUserID))
		{ 
			// No authorisation
			return false;
		}
		
		// Get endpoint details from API
		$bucket = $this->getEndpointDetails($policyID, $policyVersion)->bucket;		
		$s3 = $this->getS3Endpoint($policyID, $policyVersion);

		// Construct storage key from UUID
		$storageKey = "recording/{$this->orgID}/{$uuid}";

		// GET record from S3
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
			LogVerbose("Processing downloaded file content of {$storageKey}");
			$extractedContent = ArchivingProcessFile::ProcessFileContent($response->body, $this->encryptionKey);
		}
		catch (Exception $e)
		{
			throw new Exception("Failed to process downloaded file content of {$storageKey}: {$e}");
		}
		
		$callrecordingsDir = "{$this->baseDir}/{$this->csvFileName}-recordings";
		$wavFileRelPath = "{$this->csvFileName}-recordings/{$prefix}-{$uuid}.wav";
		$wavFilePath = "{$this->baseDir}/{$wavFileRelPath}";
		$wavFile = null;
		
		try
		{
			if (!$this->createDirectoryTree($callrecordingsDir))
				throw new Exception("Failed to create temp directory '{$callrecordingsDir}'");
			
			$wavFile = fopen($wavFilePath, 'w');
			if ($wavFile === false)
				throw new Exception ("Failed to create WAV file for writing: {$wavFilePath}");
			
			$byteCount = fwrite($wavFile, $extractedContent);
			if ($byteCount === false)
				throw new Exception ("Failed to to write to WAV file: {$wavFilePath}");
			
			LogMessage("Successfully wrote {$byteCount} bytes to {$wavFilePath}");
		}
		catch (Exception $e)
		{
			LogError($e);
			if ($wavFile !== null)
				fclose($wavFile);
			throw $e;
		}
		
		fclose($wavFile);
		
		return $wavFileRelPath;
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
	public function __construct(ArchivingExportApp $app, $exportID, $orgID, $userID, $queryQID, array $resultIDs, $zipPassword)
	{
		parent::__construct($app, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword);
	}
	
	public function type() { return 'sms-mms'; }
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
		
		LogVerbose("Processing logs result ID {$resID}, UUID: {$UUID}...");
		
		//$recording = '"None"';
		//if ($recorded == 'YES')
		{
			$prefix = $this->formatTime($time, $this->timezone, 'Y-m-d@H.i.s') . "-from-{$fromNumber}-to-{$toNumber}";
			$archMsg = $this->getArchivedMessage($logData, $res, $prefix);
			if (isset($archMsg->filepath))
			{
				LogVerbose("Fetched MMS for result ID {$resID}: {$media}");
				
				$zipFilePath = "{$this->baseDir}/{$this->csvFileName}.zip";
				if (file_exists($zipFilePath))
					$zipFilesize = (int)filesize($zipFilePath);
				else
					$zipFilesize = 0;
				
				$mmsFilesize = filesize("{$this->baseDir}/{$archMsg->filepath}");
				if (($mmsFilesize > 0) && (($zipFilesize+$mmsFilesize) >= ($this->app->zipFilesizeLimitMB * (1024.0*1024.0))))
				{
					LogWarning("ZIP archive would exceed {$app->this->zipFilesizeLimitMB} MB file size limit - truncating export zip before result ID {$resID}", null, 'U:ARCX');
					return false;
				}
				
				$this->addToZipArchive($archMsg->filepath);
				
				//$message = "=HYPERLINK(\"{$archMsg->filepath}\", \"Click to view\")"; // haven't figured out how to get this working in a CSV file
				$message = "=HYPERLINK(\"{$archMsg->filepath}\")";
			}
			else if (isset($archMsg->text))
			{
				LogVerbose("Fetched SMS for result ID {$resID}: {$media}");
				
				$text = str_replace("\"", "\"\"", $archMsg->text);
				$message = "\"{$text}\"";
			}
			else if ($archMsg === false)
			{
				LogWarning("No authorisation to media for result ID {$resID}, UUID {$UUID} - skipping media file", null, 'U:ARCX');
				$message = '"Access Denied"'; // user doesn't have permission to access file
			}
			else // if ($archMsg === null)
			{
				LogWarning("Media for result ID {$resID}, UUID {$UUID} not available (hasn't been archived yet?)", null, 'U:ARCX');
				$message = '"Not Available"'; // i.e. hasn't been archived yet
			}
		}
				
		$timeLocal = $this->formatTime($time, $this->timezone, 'Y-m-d H:i:s');
		$timeUTC = gmstrftime('%F %T', $time);
								
		$csvEntryFields = array
		(
			$message,		// Message
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
				// don't quote media field as HYPERLINK macro won't work
				$csvEntryField = "\"{$csvEntryField}\"";
		}
		$csvEntryLine = implode(',', $csvEntryFields);
		
		if (fwrite($this->csvFile, $csvEntryLine."\r\n") === false)
			throw new Exception("Failed to write entry line to CSV file");
		
		return true;
	}
	
	private function getArchivedMessage(array $logData, array $res, $prefix)
	{
		$resID = $logData['ResID'];
		
		$uuid = false;
		$policyVersion = false;
		$policyID = false;
		$ownerUserID = false;

		if (!empty($res['RetentionUUID']))
		{
			if ($res['RetentionState'] == 'Available' && $res['RetentionPurged'] == 'no') 
			{
				$uuid = $logData['UUID'];
				$policyVersion = $res['RetentionPolicyVersion'];
				$policyID = (int)$res['RetentionPolicyID'];
				$ownerUserID = (int)$res['RetentionUserID'];
			}
			else
			{
				LogUWarning("Processing result ID {$resID}: retention entry for UUID '{$logData['UUID']}' not available: State: {$res['RetentionState']}, Purged: {$res['RetentionPurged']}", null, 'U:ARCX');
			}
		}
		else
		{
			LogWarning("Processing result ID {$resID}: no retention entry found for UUID '{$logData['UUID']}'", null, 'U:ARCX');
		}
		
		if (!$uuid)
		{
			// media not available
			return null;
		}
					
		if (!$this->checkArchivingPolicyPermission($policyID, $ownerUserID))
		{ 
			// No authorisation
			return false;
		}
		
		// Get endpoint details from API
		$bucket = $this->getEndpointDetails($policyID, $policyVersion)->bucket;		
		$s3 = $this->getS3Endpoint($policyID, $policyVersion);

		// Construct storage key from UUID
		$storageKey = "sms/{$this->orgID}/{$uuid}";

		// GET record from S3
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
			LogVerbose("Processing downloaded file content of {$storageKey}");
			$extractedContent = ArchivingProcessFile::ProcessFileContent($response->body, $this->encryptionKey);
		}
		catch (Exception $e)
		{
			throw new Exception("Failed to process downloaded file content of {$storageKey}: {$e}");
		}
		
		$message = new stdClass();
		
		// seems crude, but this is exactly how the portal determines if message is SMS or MMS
		if ((strpos($extractedContent, "<?xml") === 0) || (strpos($extractedContent, "<?xml") === 1)) 
		{
			// SMS message - extract text field from XML
			
			//LogDebug("SMS XML: {$extractedContent}");
			
			$smsDom = simplexml_load_string($extractedContent);
			if (!($smsDom instanceof SimpleXMLElement)) 
				throw new Exception("Failed to parse SMS XML details");
			
			$textNode = $smsDom->xpath('Text[1]');
			if (empty($textNode)) 
				throw new Exception("Failed to extract text from SMS XML details");
			$text = (string)$textNode[0];			
			
			$message->text = html_entity_decode($text);//, ENT_COMPAT | ENT_HTML401, "UTF-8");
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
				
				$mmsFile = fopen($mmsFilePath, 'w');
				if ($mmsFile === false)
					throw new Exception ("Failed to create MMS file for writing: {$mmsFilePath}");
				
				$byteCount = fwrite($mmsFile, $extractedContent);
				if ($byteCount === false)
					throw new Exception ("Failed to to write to MMS file: {$mmsFilePath}");
				
				LogMessage("Successfully wrote {$byteCount} bytes to {$mmsFilePath}");
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
						$value = (int)$value;
						if ($value > 0)
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
	public $zipFilesizeLimitMB = null;

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
		
		$this->zipFilesizeLimitMB = (double)$this->conf->get('export.zipfilesizelimitmb', 100);
		
		$coreapi = $this->conf->get('coreapi.url', 'http://api.{hapi:SearchDomain}');
		if (empty($coreapi))
			throw new Exception('CoreAPI URL not configured');
		
		$authorizationSentry = null;
		$requests = array();
		try
		{
			API::Init($coreapi, new ArrayStorage(), APP_NAME_.':1.0');
			
			API::LoginBasicAuth($authCreds, null/*mfa*/, true/*re_authenticate*/);
			
			$this->api = array($coreapi, API::GetToken());
			/** @noinspection PhpUnusedLocalVariableInspection */
			$authorizationSentry = new ScopeSentry(
				function()
				{
					//unset($this->api);
					API::LogOut();
				}
			);
			
			// Get CoreAPI SQL connection
			$this->coreDbc = MySQLiConnection::Get('coresql');
			
			// Get Big SQL database connection
			$this->bigDbc = MySQLiConnection::Get('retention_rw');
			
			$requests = $this->getPendingExportRequests();
		}	
		catch (Exception $e)
		{
			LogWarning("Failed to run archiving exporter: Exception: {$e}");	
			return 1;
		}
		
		if (is_array($requests))
		{
			if (count($requests) == 0) 
			{
				LogVerbose("No pending archiving export requests to service");	
				return 0;
			}
			
			foreach ($requests as $request)
			{
				$exportID = (int)$request['ExportID'];
				$orgID = (int)$request['OrgID'];
				$userID = (int)$request['UserID'];
				$type = $request['Type'];
				$queryQID = $request['QueryQID'];
				$resultIDs = array();
				$resIDs = explode(',', $request['ResultIDs']);
				$serviceFailedCount = (int)$request['ServiceFailedCount'];
				foreach ($resIDs as $id)
				{
					$id = (int)$id;
					if ($id > 0)
						$resultIDs[$id] = $id;
				}
				$zipPassword = $request['Password'];
				
				try
				{
					switch ($type)
					{
						case 'Call':							
						case 'SMS':
							break; // OK
						default:
							throw new Exception("Unhandled export type '{$type}'");
					}
					
					$this->updateExportStatus($exportID, 'Processing');
					
					$className = "Archiving{$type}Export";
					$export = new $className($this, $exportID, $orgID, $userID, $queryQID, $resultIDs, $zipPassword);					
					$result = $export->generate();
							
					LogMessage("Successfully exported ".$export->type()." logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: ExportURI: {$result->exportURI}");					
					
					$this->updateExportStatus($exportID, 'Complete', $result);
				}
				catch (Exception $e)
				{
					LogWarning("Failed to export {$type} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: Exception: {$e}", null, 'U:ARCX');	
					$this->updateExportStatus($exportID, 'Failed', $serviceFailedCount);
				}
			}
		}
		
		return 0;
	}
	
	private function getPendingExportRequests()
	{
		$this->cleanupExportStatus();
		
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
				
		$result = $this->bigDbc->query(
			"SELECT `ExportID`, `Submitted`, `OrgID`, `UserID`, `Type`, `QueryQID`, `ResultIDs`, aes_decrypt(`EncPwd`, concat(`ExportID`, `OrgID`, '{$this->passwordAESKey}', `QueryQID`)) AS `Password`, `ServiceFailedCount` ".
			"FROM `Aux`.`LogsExportQ` ".
			"WHERE Status='Pending' ". 
			"AND `ClientLocation`='{$locName}' ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT 100;"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		return $result;	
	}
		
	private function cleanupExportStatus()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		$this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='Pending' ".
			"WHERE `Status`='Processing' ".
			"AND `ClientLocation`='{$locName}';"
		);
	}
	
	private function updateExportStatus($exportID, $status, $result=null)
	{
		static $startTime; 
		$additionalFields = '';
		
		switch ($status)
		{
			case 'Processing':
				$startTime = microtime(true/*asFloat*/);
				$additionalFields = ", `LastServiceAttempt`=NOW(), NextServiceAttempt=NULL ";
				break;
				
			case 'Complete':
				//$additionalFields .= ", `EncPwd`=NULL ";
				if (is_object($result))
				{
					if (isset($result->result) && (strlen($result->result) > 0))
						$additionalFields .= ", `Result`='{$result->result}' ";
					if (isset($result->policyID))
						$additionalFields .= ", `PolicyID`='{$result->policyID}' ";
					if (isset($result->exportURI))
						$additionalFields .= ", `ExportURI`='{$result->exportURI}' ";
				}
				
				$elapsedMSecs = (int)((microtime(true/*asFloat*/) - $startTime) * 1000.0);
				$additionalFields .= ", `DurationMS`={$elapsedMSecs} ";
				
				$additionalFields .= ", `ServiceFailedCount`=NULL ";
				break;
				
			case 'Failed':
				//$additionalFields .= ", `EncPwd`=NULL ";
				$elapsedMSecs = (int)((microtime(true/*asFloat*/) - $startTime) * 1000.0);
				$additionalFields .= ", `DurationMS`={$elapsedMSecs} ";
				
				++$result;
				$additionalFields .= ", `ServiceFailedCount`={$result} ";
				break;
				
			default:
				$additionalFields = '';
				break;
		}
		
		$status = $this->bigDbc->escape_string($status);
		
		$this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='{$status}' ". 
			$additionalFields .
			"WHERE `ExportID`={$exportID};"
		);
	}	
}

/**
 * Show usage help, optionally with error messag
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
	cli::init(null, array('auth-creds:', 'auth-hash:', 'conf:', 'help', 'no-daemon'));
	
	if (true === cli::get('help', false))
	{
		Usage();
		exit(0);
	}
	
	$app = ArchivingExportApp::getInstance(cli::get('--conf', ArchivingExportApp::CONFIG_PATH));	
	$conf = $app->getConf();
	
	if (false === cli::get('--no-daemon', false))
	{
		DaemonScripts::Initialise(
			array(
				'runasUid' => $conf->get('app.run_as_user', 'cdrmunch'),
				'runasGid' => $conf->get('app.run_as_group', 'cdrmunch'),
				'pidfilePath' => $conf->get('app.pid_file', '/var/run/archiving/rmarchexport.pid'),
				'signalHandlers' => array(SIGTERM => $terminateCallback, SIGINT => $terminateCallback),
				'sleepInterval' => array(3 /*seconds and*/, 0 /* micro second */),
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
