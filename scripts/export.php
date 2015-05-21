#!/usr/bin/php
<?php

/**
* ArchivingExtractApp
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
define('APP_NAME_', 'rm-archiving-export');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);
define('RM_CLI_APP', true);

define('ARCHIVING_FILE_TYPE_UNKNOWN', 0);
define('ARCHIVING_FILE_TYPE_CDR', 1);
define('ARCHIVING_FILE_TYPE_RECORDING', 2);

require_once 'rmapp/class.BaseApp.php';
require_once 's3/S3.php';

require_once 'restclient/RestClient.class.php';
require_once 'hPHP/hapi.php';

/**
* ArchivingExportApp
*
* @category  ArchivingExportApp
* @package   RM-archiving
* @author    Greg Inglis <greg.inglis@redmatter.com>
* @copyright 2009-2015 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingExportApp extends BaseApp
{
	//protected $org = null;
	private $api = null;
	private $endpointDetails = array();
	private $endpoints = array();

	private $coreDbc = null;
	private $bigDbc = null;
	
	private $hAPISearchDomain = null;
	private $hAPILocName = null;
	
	private $passwordAESKey = null;
	private $zipFilesizeLimitMB = null;

	/**
	 * run
	 *
	 * @tnhrows Exception
	 *
	 * @return void
	 */
	public function run()
	{
		$hAPIHost = new HAPIHost();
		$this->hAPISearchDomain = $hAPIHost->get('SearchDomain');
		$this->hAPILocName = $hAPIHost->get('LocName');
		
		if ($hAPIHost->InProduction())
			$this->passwordAESKey = sha1($this->conf->get('coreapi.logsexportq.aeskey', false));	
		else
			$this->passwordAESKey = sha1('UNSECURE');
			
		$this->zipFilesizeLimitMB = (int)$this->conf->get('export.zipfilesizelimitmb', 100);
				
		$config = $this->conf;
/*
		if (false === ($orgId = cli::get('--org', $config->get('extract.org_id', false))))
			throw new Exception('OrgID not given');

		if (false === ($caseRef = cli::get('--case-ref', $config->get('extract.case_reference', false))))
			throw new Exception('Case reference not given');

		if (false === ($outDir = cli::get('--out-dir', $config->get('extract.output_dir', false))))
			throw new Exception('Output dir not given');
*/			
		if (false === ($authCreds = cli::get('--auth-creds', $this->conf->get('coreapi.authorization_creds', false))))
			throw new Exception('CoreAPI Auth creds not given');
/*
		if (false === ($username = cli::get('--user', $config->get('extract.username', false))))
			throw new Exception('Username not given');

		if (false === ($password = cli::get('--password', $config->get('extract.password', false))))
			throw new Exception('Password not given');

		$extractTypes = cli::get('--type', $config->get('extract.type', 'recording'));
		$extractTypes = preg_split('/[;, ]+/', $extractTypes);
		$extractTypes = array_map(
			function($str)
			{
				$int = Archiving::GetFileType($str);
				if ($int === Archiving::FILE_TYPE_UNKNOWN)
					throw new Exception('Unknown file-type '.$str);

				return $int;
			},
			$extractTypes
		);

		if (empty($extractTypes))
			throw new Exception('No valid file-types specified');

		$passcode = cli::get('--passcode', null);

		$startDate = cli::get('--start-date', $config->get('extract.start_date', null));
		$endDate = cli::get('--end-date', $config->get('extract.end_date', null));
*/
		$authorization = null;
		$coreapi = $this->conf->get('coreapi.url', 'http://api.{hapi:SearchDomain}');
		API::Init($coreapi, new ArrayStorage(), APP_NAME_.':1.0');
		
		//LogMessage("authCreds[{$authCreds}]");
		
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
		
		// Get Core SQL connection
		$this->coreDbc = MySQLiConnection::Get('coresql');
		
		//$this->coreapi_dbc = MySQLiConnection::Get('coreapi_ro');

		// Get Big SQL database connection
		//$this->big_dbc = MySQLiConnection::Get('bigsql');
		$this->bigDbc = MySQLiConnection::Get('retention_ro');
		
		$requests = $this->getPendingExportRequests();
		if (is_array($requests))
		{
			foreach ($requests as $request)
			{
				$exportId = (int)$request['ExportID'];
				$orgId = (int)$request['OrgID'];
				$userId = (int)$request['UserID'];
				$type = $request['Type'];
				$queryQID = $request['QueryQID'];
				$resultIDs = array();
				$resIDs = explode(',', $request['ResultIDs']);
				foreach ($resIDs as $id)
				{
					$id = (int)$id;
					if ($id > 0)
						$resultIDs[$id] = $id;
				}
				$zipPassword = $request['Password'];
				
				try
				{
					$this->updateExportStatus($exportId, 'Processing');
					
					switch ($type)
					{
						case 'Call':
							$result = $this->exportCallLogs($exportId, $orgId, $userId, $queryQID, $resultIDs, $zipPassword);
							
							LogMessage("Successfully exported call logs: ExportID: {$exportId}, OrgID: {$orgId}, UserID: {$userId}, QueryQID: {$queryQID}: ExportURI: {$result->exportURI}");
							break;
						
						case 'SMS':
							// TODO:
							//$result = $this->exportSMSLogs($exportId, $orgId, $userId, $queryQID, $resultIDs, $zipPassword);
							//break;
							
						default:
							throw new Exception("Unhandled export type '{$type}'");
							break;
					}
					
					$this->updateExportStatus($exportId, 'Complete', $result);
				}
				catch (Exception $e)
				{
					LogWarning("Failed to export {$type} logs: ExportID: {$exportId}, OrgID: {$orgId}, UserID: {$userId}, QueryQID: {$queryQID}: Exception: {$e}", null, 'U:ARCX');	
					$this->updateExportStatus($exportId, 'Failed');
				}
			}
		}
		exit(0);
	}
	
	private function getPendingExportRequests()
	{
		$this->cleanupExportStatus();
		
		$locName = $this->coreDbc->escape_string($this->hAPILocName);
				
		$result = $this->coreDbc->query(
			"SELECT `ExportID`, `Submitted`, `OrgID`, `UserID`, `Type`, `QueryQID`, `ResultIDs`, aes_decrypt(`EncPwd`, concat(`ExportID`, `OrgID`, '{$this->passwordAESKey}', `QueryQID`)) AS `Password` ".
			"FROM `Aux`.`LogsExportQ` ".
			"WHERE Status='Pending' ". 
			"AND `ClientLocation`='{$locName}' ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT 100;"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		return $result;	
	}
		
	private function cleanupExportStatus()
	{
		$locName = $this->coreDbc->escape_string($this->hAPILocName);
		
		$this->coreDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='Pending' ".
			"WHERE `Status`='Processing' ".
			"AND `ClientLocation`='{$locName}';"
		);
	}
	
	private function updateExportStatus($exportId, $status, $result=null)
	{
		static $startTime; 
		$additionalFields = '';
		
		switch ($status)
		{
			case 'Processing':
				$startTime = microtime(true/*asFloat*/);
				$additionalFields = ", `LastServiceAttempt`=NOW() ";
				break;
			case 'Complete':
				if (is_object($result))
				{
					if (isset($result->result) && (strlen($result->result) > 0))
						$additionalFields .= ", `Result`='{$result->result}' ";					
					if (isset($result->policyID))
						$additionalFields .= ", `PolicyID`='{$result->policyID}' ";
					if (isset($result->exportURI))
						$additionalFields .= ", `ExportURI`='{$result->exportURI}' ";
				}
				//break;
			case 'Failed':
				$elapsedMSecs = (int)((microtime(true/*asFloat*/) - $startTime) * 1000.0);
				$additionalFields .= ", `DurationMS`={$elapsedMSecs} ";
				break;
			default:
				$additionalFields = '';
				break;
		}
		
		$status = $this->coreDbc->escape_string($status);
		
		$this->coreDbc->query(
			"UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='{$status}' ". 
			$additionalFields .
			"WHERE `ExportID`={$exportId};"
		);
	}
		
	private function exportCallLogs($exportId, $orgId, $userId, $queryQID, $resultIDs, $zipPassword)
	{
		LogMessage("Starting export of call logs: ExportID: {$exportId}, OrgID: {$orgId}, UserID: {$userId}, QueryQID: {$queryQID}");
		
		//throw new Exception("Testing");
		
		$baseDir = false;
		$csvFile = false;
		$zipStorageKey = false;
		try
		{
			// Get info/settings for this user/org
			
			$encryptionKey = $this->getOrgEncryptionKey($orgId);
			$policyID = $this->getOrgDefaultRecordingPolicyID($orgId);
			$timeZone = $this->getUserTimeZone($userId);
					
			// OK and now the actual call logs

			$searchStartTime = false;
			$searchEndTime = false;
			$callLogsData = array();
			$this->getApiCallLogsData($orgId, $userId, $queryQID, $searchStartTime, $searchEndTime, $callLogsData);

			$searchStartTime = $this->formatTime($searchStartTime, $timeZone, 'Y-m-d@H.i.s');
			$searchEndTime = $this->formatTime($searchEndTime, $timeZone, 'Y-m-d@H.i.s');

			//echo "logsData:\n".print_r($logsData);

			$dbRetResults = $this->getCallLogsAndRetentionDbResults($orgId, $userId, $queryQID);

			LogMessage("Processing ".count($dbRetResults)." call log entries...");

			$baseDir = "/tmp/redmatter/archiving/export/{$exportId}";
			// ensure directory is removed first
			$this->cleanDirectory($baseDir);
			if (!$this->createDirectoryTree($baseDir))
				throw new Exception("Failed to create temp directory '{$baseDir}'");
			$exportUUID = UUID::v4();
			$csvFileName = "call-export-{$searchStartTime}-to-{$searchEndTime}-{$exportUUID}";
			$csvFilePath = "{$baseDir}/{$csvFileName}.csv";

			$csvFile = fopen($csvFilePath, 'w');
			if ($csvFile === false)
				throw new Exception("Failed to open CSV file for writing '{$csvFilePath}'");

			$csvHeaderLine = implode(',', $this->csvHeaderFields);
			if (fwrite($csvFile, $csvHeaderLine."\r\n") === false)
				throw new Exception("Failed to write header to CSV file '{$csvFilePath}'");

			foreach ($dbRetResults as $dbRetResult)
			{
				$resID = (int)$dbRetResult['ResID'];
				if (empty($resultIDs) || isset($resultIDs[$resID]))
				{	
					$callLogData = &$callLogsData[$resID];
					$success = $this->exportCallLogEntry($orgId, $encryptionKey, $baseDir, $timeZone, $callLogData, $dbRetResult, $csvFile, $csvFileName, $zipPassword);
					if ($success == false)
					{
						// this will because we have reached zip file size limit
						break;
					}
				}
			}

			fclose($csvFile);
			$csvFile = false;
			
			// finally add the csv file to the archive
			$this->addToZipArchive($baseDir, "{$csvFileName}.zip", "{$csvFileName}.csv", $zipPassword);
			
			//$this->createZipArchive($baseDir, $csvFileName, "{$csvFileName}-recordings/*.wav", $zipPassword);
			
			$zipFilePath = "{$baseDir}/{$csvFileName}.zip";
			
			$exportURI = "export/{$orgId}/{$exportUUID}";
			
			$this->uploadZipArchive($orgId, $policyID, $zipFilePath, $exportURI);
		}
		catch (Exception $e)
		{
			// cleanup
			
			if ($csvFile !== false)
				fclose($csvFile);
												
			if (strlen($baseDir) > 0)
				//$this->cleanDirectory($baseDir);
				
			throw $e;
		}
		
		if (strlen($baseDir) > 0)
		{
			//$this->cleanDirectory($baseDir);
		}
		
		$result = new stdClass();
		$result->policyID = $policyID;
		$result->exportURI = $exportURI;
		$result->result = 'OK';
		
		return $result;
	}

	private function exportCallLogEntry($orgId, $encryptionKey, $baseDir, $timeZone, $logData, $res, $csvFile, $csvFileName, $zipPassword)
	{		
		$time = (int)$logData['Time'];
		$fromNumber = $logData['From_Number'];
		$toNumber  = $logData['To_Number'];
		$recorded = $logData['Recorded'];
		$resID = (int)$res['ResID'];
				
		LogDebug("Processing logs result ID {$resID} (Recorded: '{$recorded})...");
		
		$recording = '';
		if ($recorded == 'YES')
		{			
			$prefix = $this->formatTime($time, $timeZone, 'Y-m-d@H.i.s') . "-from-{$fromNumber}-to-{$toNumber}";
			$recording = $this->getCallRecording($orgId, $encryptionKey, $baseDir, $logData, $res, $prefix, $csvFileName);
			if (strlen($recording) > 0)
			{
				// TODO check if zip file too big
				$zipFilePath = "{$baseDir}/{$csvFileName}.zip";
				if (file_exists($zipFilePath))
					$zipFilesize = (int)filesize($zipFilePath);					
				else
					$zipFilesize = 0;
				
				$recordingFilesize = filesize("{$baseDir}/{$recording}");		
				if (($recordingFilesize > 0) && (($zipFilesize+$recordingFilesize) >= ($this->zipFilesizeLimitMB * (1024.0*1024.0))))
				{
					LogWarning("ZIP archive would exceed {$this->zipFilesizeLimitMB} MB file size limit - truncating export zip before result ID {$resID}", null, 'U:ARCX');
					return false;
				}
				
				$this->addToZipArchive($baseDir, "{$csvFileName}.zip", $recording, $zipPassword);
								
				//$options = "=HYPERLINK(\"{$recordingRelPath}\", \"Click to listen\")";
				$recording = "=HYPERLINK('{$recording}')";
			}
			else if ($recording === false)
			{
				LogWarning("No authorisation to recording for result ID {$resID} - skipping recording file", null, 'U:ARCX');
				$recording = 'Unauthorised'; // user doesn't have permission to access file
			}
			else // if ($recording === null)
			{
				LogWarning("Recording for result ID {$resID} not available (hasn't been archived yet?)", null, 'U:ARCX');
				$recording = 'Not Available'; // i.e. hasn't been archived yet
			}
		}
		
		//$logData = $logsData[$resID];
		
		$AUUID = $logData['AUUID'];
		$BUUID = $logData['BUUID'];	
		//$time = (int)$logData['Time'];
		$policy = $logData['Policy'];
		$from = $logData['From_User'];
		$fromDev = $logData['From_Dev'];
		//$fromNumber = $logData['From_Number'];
		$fromHangup = $logData['From_HangUp'];
		$toUser = $logData['To_User'];
		$toDev = $logData['To_Dev'];
		$toNumberDialled = $logData['To_NumberDialled'];
		//$toNumber  = $logData['To_Number'];
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

		$startTimeLocal = $this->formatTime($time, $timeZone, 'Y-m-d H:i:s');
		$startTimeUTC = gmstrftime('%F %T', $time);
		$endTimeLocal = $this->formatTime($time+$timeHuntingSecs+$timeRingingSecs+$timeTalkingSecs, $timeZone, 'Y-m-d H:i:s');
		
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
		
		foreach ($csvEntryFields as &$csvEntryField)
		{
			$csvEntryField = "\"{$csvEntryField}\"";
		}
		$csvEntryLine = implode(',', $csvEntryFields);
		
		if (fwrite($csvFile, $csvEntryLine."\r\n") === false)
			throw new Exception("Failed to write entry line to CSV file");
		
		// OK	
		return true;
	}

	private function getUserTimeZone($userId)
	{
		$client = RestClient::createClient();
		$client->setUrl('http://api.'.$this->hAPISearchDomain.'/profiles/User/'.$userId);
		$client->setMethod('GET');
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);

		$client->execute();
		
		$profilePesponse = trim($client->getResponse());
		if (empty($profilePesponse))
			throw new Exception("Failed to fetch user profile details for user ID {$userId} (for determining timezone)");
		
		//echo "profilePesponse:\n{$profilePesponse}\n";

		$profileResponseDom = simplexml_load_string($profilePesponse);
		if (!($profileResponseDom instanceof SimpleXMLElement)) 
			throw new Exception("Failed to fetch user profile details for user ID {$userId}");
		
		$timeZone = null;
		$timeZoneValNode = $profileResponseDom->xpath('General[1]/DefTZ[1]/@Val');
		if (!empty($timeZoneValNode)) 
			$timeZone = (string)$timeZoneValNode[0];
		
		if (empty($timeZone))
		{
			$timeZoneDefaultNode = $profileResponseDom->xpath('General[1]/DefTZ[1]/@Default');
			if (!empty($timeZoneDefaultNode))
				$timeZone = (string)$timeZoneDefaultNode[0];
		}
		
		if (empty($timeZone))
		{			
			$timeZone = 'Europe/London';
			LogMessage("Couldn't determine time zone for user ID {$userId} - defaulting to {$timeZone}");
		}
		
		return new DateTimeZone($timeZone);
	}

	private function getApiCallLogsData($orgId, $userId, $queryQID, &$searchStartTime, &$searchEndTime, &$logsData)
	{
		$client = RestClient::createClient();
		$client->setUrl('http://api.'.$this->hAPISearchDomain.'/newlogs/download');
		$client->setMethod('POST');	
		$headers = array("Authorization" => API::GetToken());
		$client->setHeaders($headers);

		$logsParamsXml = "<Parameters QID=\"{$queryQID}\" Max=\"8000\" Offset=\"0\" ShowMappings=\"YES\" UserFormat=\"first last\" ArchivingForUserID=\"{$userId}\"/>";
		$client->setParameters($logsParamsXml);
		
		$client->execute();
		
		$logsResponse = trim($client->getResponse());
		//echo "logsResponse:\n{$logsResponse}\n";
		
		$logsResponseDom = simplexml_load_string($logsResponse);
		if (!($logsResponseDom instanceof SimpleXMLElement))
			throw new Exception("Failed to fetch logs result for query QID {$queryQID}");
		
		$searchStartTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[1]/@Val');
		$searchEndTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[2]/@Val');
		if (empty($searchStartTimeNodes) || empty($searchEndTimeNodes))
			throw new Exception("Failed to fetch logs search criteria for query QID {$queryQID}");
		$searchStartTime = (int)$searchStartTimeNodes[0];
		$searchEndTime = (int)$searchEndTimeNodes[0];
		
		LogDebug("searchStartTime: {$searchStartTime}");
		LogDebug("searchEndTime: {$searchEndTime}");
		
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
			throw new Exception("Failed to fetch logs result for logs query QID {$queryQID}");
		
		$this->getLogsData($logsResponseDom, $fields, $users, $devs, $policies, $logsData);	
	}
	
	private function getOrgEncryptionKey($orgId)
	{
		$result = $this->coreDbc->query(
			"SELECT `EncryptionKey` FROM `Archiving`.`Provisioning` WHERE `OrgID`={$orgId} LIMIT 1"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		if (empty($result) || empty($result[0]) || empty($result[0]['EncryptionKey']))
			throw new Exception("Could not get encryption key for Org {$orgId}");

		return $result[0]['EncryptionKey'];	
	}

	private function getOrgDefaultRecordingPolicyID($orgId)
	{
		return $this->getOrgDefaultPolicyIDForType($orgId, 'recording');
	}
	
	private function getOrgDefaultSMSPolicyID($orgId)
	{
		return $this->getOrgDefaultPolicyIDForType($orgId, 'sms');
	}	
	
	private function getOrgDefaultPolicyIDForType($orgId, $type)
	{
		$result = $this->coreDbc->query(
			"SELECT m.PolicyID ".
			"FROM Archiving.DefaultPolicyMap m ".
			"JOIN Archiving.PolicyTypes t ON m.PolicyTypesID=t.ID ". 
			"WHERE m.OrgID={$orgId} ". 
			"AND t.Name='{$type}' ".
			"AND t.Internal='no';"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		if (empty($result) || empty($result[0]) || empty($result[0]['PolicyID']))
			throw new Exception("Could not fetch default {$type} policy ID for Org {$orgId}");
		return (int)$result[0]['PolicyID'];	
	}	
	
	private function getCallLogsAndRetentionDbResults($orgId, $userId, $queryQID)
	{
		return $this->bigDbc->query(
			"SELECT \n" .
			"LOWER(HEX(l.`AUUID`)) AS `HexAUUID`, \n" .
			"LOWER(HEX(l.`BUUID`)) AS `HexBUUID`, \n" .
			"l.*, \n" .
			"ra.`LegUUID` AS `RetentionALegUUID`, \n" .
			"ra.`PolicyID` AS `RetentionALegPolicyID`, \n" .
			"ra.`PolicyVersion` AS `RetentionALegPolicyVersion`, \n" . 
			"ra.`State` AS `RetentionALegState`, \n" . 
			"ra.`Purged` AS `RetentionALegPurged`, \n" . 
			"rb.`LegUUID` AS `RetentionBLegUUID`, \n" .
			"rb.`PolicyID` AS `RetentionBLegPolicyID`, \n" .
			"rb.`PolicyVersion` AS `RetentionBLegPolicyVersion`, \n" . 
			"rb.`State` AS `RetentionBLegState`, \n" . 
			"rb.`Purged` AS `RetentionBLegPurged` \n" .
			"FROM `Org_{$orgId}`.`Results_Logs_{$userId}_{$queryQID}` l \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$orgId}` ra ON (l.`AUUID`=ra.`LegUUID`) \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$orgId}` rb ON (l.`BUUID`=rb.`LegUUID`)"
		)->fetch_all(MySQLiResult::AS_ASSOC);
	}

	private function getCallRecording($orgId, $encryptionKey, $baseDir, $logData, $res, $prefix, $csvFileName)
	{
		$resID = $logData['ResID'];
		
		$uuid = false;
		$policyVersion = false;
		$policyID = false;

		if (!empty($res['RetentionALegUUID']))
		{
			if ($res['RetentionALegState'] == 'Available' && $res['RetentionALegPurged'] == 'no') 
			{
				$uuid = $logData['AUUID'];
				$policyVersion = $res['RetentionALegPolicyVersion'];
				$policyID = $res['RetentionALegPolicyID'];
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
				$policyID = $res['RetentionBLegPolicyID'];
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
			return null;
		
		//LogDebug("Loading archiving policy ID {$policyID}");
		//$policy = new ArchivingPolicy($this->coreDbc, $orgId, $policyID, 0/*$policyVersion*/, true, null/*$currentUser*/);
		//if (ArchivingPolicy::Check()
		//	return false;
				
		//LogDebug("Processing result ID {$resID}: call is recorded retention entry UUID '{$uuid}'", null, 'D:ARCX');
		/*
		$uuid = substr($uuid, 0, 8) .
			'-' . substr($uuid, 8, 4) .
			'-' . substr($uuid, 12, 4) .
			'-' . substr($uuid, 16, 4) .
			'-' . substr($uuid, 20);*/
/*					
		echo "========== ArchivingChecker::Check $uuid...\n";
		
		$obj = ArchivingChecker::Check(ARCHIVING_FILE_TYPE_RECORDING, 2, $uuid);
		
		echo "========== obj:".print_r($obj)."\n";
		
		echo "========== ArchivingChecker::Check $uuid DONE\n";
*/

		// Get endpoint details from API
		$bucket = $this->getEndpointDetails($orgId, $policyID, $policyVersion)->bucket;		
		$s3 = $this->getS3Endpoint($orgId, $policyID, $policyVersion);

		// Construct storage key from UUID
		$storageKey = "recording/{$orgId}/{$uuid}";

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
			$extractedContent = ArchivingProcessFile::ProcessFileContent($response->body, $encryptionKey);
		}
		catch (Exception $e)
		{
			throw new Exception("Failed to process downloaded file content of {$storageKey}: {$e}");
		}
		
		$callrecordingsDir = "{$baseDir}/{$csvFileName}-recordings";
		$wavFileRelPath = "{$csvFileName}-recordings/{$prefix}-{$uuid}.wav";
		$wavFilePath = "{$baseDir}/{$wavFileRelPath}";
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
	
	private function getLogsIDMap($dom, $xpath, &$map)
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

	private function getLogsData($dom, $fields, $users, $devs, $policies, &$data)
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
	
	private function addToZipArchive($baseDir, $zipFileName, $relFileSpec, $zipPassword)
	{	
		chdir($baseDir);
		
		$cmd = "7za a {$zipFileName} {$relFileSpec}";
		LogVerbose("Adding file {$relFileSpec} to export zip '{$zipFileName}' with command: {$cmd} -p********");
		
		$cmd .= " -p{$zipPassword}";
		
		$output = array();
		$return_var = 0;
		$zipRes = exec($cmd, $output, $return_var);
		
		$zipFilePath = "{$baseDir}/{$zipFileName}";
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
		
		LogDebug("Added file {$relFileSpec} to export zip '{$zipFileName}' OK");
	}	
	/*
	private function createZipArchive($baseDir, $csvFileName, $mediaFileSpec, $zipPassword)
	{	
		chdir($baseDir);
		
		$cmd = "7za a {$csvFileName}.zip {$csvFileName}.csv {$mediaFileSpec}";
		LogVerbose("Zipping up export archive with command: {$cmd} -p********");
		
		$cmd .= " -p{$zipPassword}";
		
		$output = array();
		$return_var = 0;
		$zipRes = exec($cmd, $output, $return_var);
		
		$zipFilePath = "{$baseDir}/{$csvFileName}.zip";
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
					
		LogVerbose("Zipped up export archive OK");	
	}
	*/

	private function uploadZipArchive($orgId, $policyID, $zipFilePath, $storageKey)
	{		
		$bucket = $this->getEndpointDetails($orgId, $policyID, 0/*policyVersion*/)->bucket;
		$s3 = $this->getS3Endpoint($orgId, $policyID, 0/*policyVersion*/);		
		
		LogVerbose("Uploading export zip {$storageKey} to bucket {$bucket} in S3...");
		
		$headers = array();
		if (!$s3->putObjectFile($zipFilePath, $bucket, $storageKey, S3::ACL_PRIVATE, $headers, 'binary/octet-stream'))
			throw new Exception("Failed to store file '{$zipFilePath}' in S3 bucket: {$bucket}, storageKey: {$storageKey}");
					
		LogVerbose("Uploaded export zip to S3 OK");
	}
	
	private function getEndpointDetails($orgId, $policyID, $policyVersion)
	{
		if (!isset($this->endpointDetails[$policyID][$policyVersion]))
		{
			$policy = ArchivingPolicy::Get($this->api, $orgId, $policyID, $policyVersion);
			$endpointDetails = ArchivingEndpoint::Get($this->api, $orgId, $policy->storageEndpointID, true);

			if (!isset($this->endpointDetails[$policyID]))
				$this->endpointDetails[$policyID] = array();

			$this->endpointDetails[$policyID][$policyVersion] = $endpointDetails;
		}

		return $this->endpointDetails[$policyID][$policyVersion];
	}

	private function getS3Endpoint($orgId, $policyID, $policyVersion)
	{
		if (!isset($this->endpoints[$policyID][$policyVersion]))
		{
			$endpointDetails = $this->getEndpointDetails($orgId, $policyID, $policyVersion);

			if ($endpointDetails->type != 's3')
				throw new Exception('Endpoint is not on Amazon S3');

			if (!isset($this->endpoints[$policyID]))
				$this->endpoints[$policyID] = array();
				
			LogDebug("new S3 endpoint for policy ID {$policyID}: accessKey: {$endpointDetails->accessKey}, secretKey: {$endpointDetails->secretKey}");
			
			$endpoint = new S3($endpointDetails->accessKey, $endpointDetails->secretKey);
			$this->endpoints[$policyID][$policyVersion] = $endpoint;
		}
		else
		{
			LogDebug("Got existing S3 endpoint for policy ID {$policyID}: accessKey: {$policyID}");
		}

		return $this->endpoints[$policyID][$policyVersion];
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

	private function isFlagSet($flag, $AFlags, $BFlags)
	{
		if (($AFlags & (1 << $flag)) || ($BFlags & (1 << $flag)))
			return 'YES';
		else
			return '-';	
	}

	private function formatTime($time, $timeZone, $format)
	{
		$dt = new DateTime();
		$dt->setTimezone($timeZone);
		$dt->setTimestamp($time);
		return $dt->format($format);
	}

	private function createDirectoryTree($dirPath)
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
				$success = (bool)mkdir($path, 0755);
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

	private function cleanDirectory($dir)
	{
		if (strpos($dir, '/tmp/') !== 0) {
			LogWarning("Attempting to clean non-temp directory?");
			return;
		}		
		exec("rm -R -f {$dir}/");	
	}
	
	var $csvHeaderFields = array
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

try
{
	if (CLI::get('--help', false) !== false)
		usage();

	$default_conf = getenv('PWD').'/archiving-export.conf';
	echo "$default_conf\n";
	
	$config = CLI::get('--conf', $default_conf);
	echo "config:".print_r($config)."\n";
	if (!is_readable($config))
		throw new Exception("No configuration given and no config found at {$config}; use --conf option to specify the config file");

	ArchivingExportApp::getInstance($config)->run();
}
catch (Exception $e)
{
	LogWarning($e);
}
