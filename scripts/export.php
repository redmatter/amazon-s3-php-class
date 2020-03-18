#!/usr/bin/php54
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


if (!defined('CURL_SSLVERSION_DEFAULT')) {
    define('CURL_SSLVERSION_DEFAULT', 0);
}

if (!defined('CURL_SSLVERSION_TLSv1')) {
    define('CURL_SSLVERSION_TLSv1', 1);
}

if (!defined('CURL_SSLVERSION_SSLv2')) {
    define('CURL_SSLVERSION_SSLv2', 2);
}

if (!defined('CURL_SSLVERSION_SSLv3')) {
    define('CURL_SSLVERSION_SSLv3', 3);
}

require_once 'rmapp/class.BaseApp.php';
require_once 'rmdaemonscripts/DaemonScripts.php';
require_once 's3/S3.php';
require_once 'archiving/class.ArchivingProcessFile.php';
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

function cleanDirectory($dir)
{
	if (strpos($dir, '/tmp/') !== 0) {
		LogWarning('Attempting to clean non-temp directory?');
		return;
	}
	exec("rm -R -f {$dir}/");
}
	
class TruncateExportException extends Exception
{
	public function __construct()
	{
		parent::__construct();
	}
}

class ArchivingExportZip
{
	public function __construct(ArchivingExport $export, $searchStartTime, $searchEndTime)
	{
		$this->export = $export;
		$this->searchStartTime = $searchStartTime;
		$this->searchEndTime = $searchEndTime;
		
		$tempDir = $this->export->app->cliMode ? self::TEMP_CLI_DIR : self::TEMP_DIR;
		$this->baseDir = "{$tempDir}/{$this->export->exportID}";
		
		if (is_dir($this->baseDir))
			cleanDirectory($this->baseDir);
		
		// ensure tmp directory for this export ID is removed first
		//if (!$this->createDirectoryTree($this->baseDir))
		//	throw new Exception("Failed to create temp directory '{$this->baseDir}'");
		
		$this->start();
	}
	
	public function __destruct()
	{
		if (($this->csvFile !== false) && ($this->csvFile !== null))
			fclose($this->csvFile);
		
		if ($this->baseDir && is_dir($this->baseDir))
			cleanDirectory($this->baseDir);
	}
	
	private function start()
	{
		$this->count++;
		
		// ensure tmp directory for this export ID is removed first
		if (!$this->createDirectoryTree("{$this->baseDir}/{$this->count}"))
			throw new Exception("Failed to create temp directory '{$this->baseDir}/{$this->count}'");
		
		// PLAT-4141
		// Remove search time range from zip filename and WAV recordings subdirectory to shorten paths,
		// so user less likely to hit crappy Windows 260 character max path limit :-(
		//$this->fileNameBase = "org-{$this->export->orgID}_{$this->export->type()}-export-{$this->export->exportID}_from-{$this->searchStartTime}_to-{$this->searchEndTime}";		
		$this->fileNameBase = "org-{$this->export->orgID}_{$this->export->type()}-export-{$this->export->exportID}";
				
		$part = sprintf('%02d', $this->count);
		$this->zipFileName = "{$this->fileNameBase}_{$part}.zip";
		$this->csvFileName = "{$this->fileNameBase}_from-{$this->searchStartTime}_to-{$this->searchEndTime}_{$part}.csv";
		$csvFilePath = "{$this->baseDir}/{$this->count}/{$this->csvFileName}";

		$this->csvFile = fopen($csvFilePath, 'wb');
		if ($this->csvFile === false)
			throw new Exception("Failed to open CSV file for writing '{$csvFilePath}'");
			
		$csvHeaderLine = "\xEF\xBB\xBF"; // UTF-8 BOM
		$csvHeaderLine .= implode(',', $this->export->getCsvHeaderFields());
		if (fwrite($this->csvFile, $csvHeaderLine."\r\n") === false)
			throw new Exception("Failed to write header to CSV file '{$csvFilePath}'");
	}
	
	public function writeCSVEntryFields(array $fields)
	{
		$line = implode(',', $fields);	
		if (fwrite($this->csvFile, $line."\r\n") === false)
			throw new Exception("Failed to write entry line to CSV file");
	}
	
	public function canAddMediaFile($filesize, $s3CompressedFilesize)
	{
		if ($filesize > ($this->export->app->maxFileSizeMB * (1024.0*1024.0)))
		{
			// skip this exporting this entry, but keep going (as subsequent log entry media may still fit in the export zip
			LogDebug("media file size {$filesize} would exceed maximum ".($this->export->app->maxFileSizeMB * (1024.0*1024.0)));
			return false;
		}
		
		// p7zip compression gives a file size that is about 15% bigger than the S3 compressed size
		$p7zipCompressedFilesize = $s3CompressedFilesize * 1.15;
		
		// OK if the compressed media file size is bigger than our max zip file size, we can't add
		if ($p7zipCompressedFilesize > ($this->export->app->maxZipFileSizeMB * (1024.0*1024.0)))
		{
			// skip this exporting this entry, but keep going (as subsequent log entry media may still fit in the export zip
			LogDebug("media file compressed size {$p7zipCompressedFilesize} would exceed maximum ".($this->export->app->maxZipFileSizeMB * (1024.0*1024.0)));
			return false;
		}
		
		$zipFilePath = "{$this->baseDir}/{$this->count}/{$this->zipFileName}";
		if (file_exists($zipFilePath))
		{
			// OK REALLY REALLY important to call clearstatcache() first other was filesize() may return an earlier file size!:
			// 'On Linux based systems, data fetched by filesize() is "statcached".
			// Try calling clearstatcache(); before the filesize call.'
			// http://stackoverflow.com/questions/3747982/php-filesize-reporting-old-size
			clearstatcache();
			
			$zipFilesize = filesize($zipFilePath);
			
			if (($zipFilesize > 0) && ($p7zipCompressedFilesize > 0) && (($zipFilesize+$p7zipCompressedFilesize) > ($this->export->app->maxZipFileSizeMB * (1024.0*1024.0))))
			{
				// adding media file to current zip would exceed zip file size maximum
				
				if ($this->count >= $this->export->app->maxZipFileCount)
					// ... we are also at max zip file count so truncate export job before this entry
					throw new TruncateExportException();
				
				// ... finish current zip archive and start a new one
				LogVerbose("{$this->context()}Current zip archive full - starting new zip archive number ".($this->count+1)."");
				$this->end();
				$this->start();
			}
		}
		
		return true;
	}
	
	public function addMediaFileContent($dirSuffix, $fileName, $content)
	{
		$fileRelPath = $this->writeMediaFileContent($dirSuffix, $fileName, $content);
		$this->addFile($fileRelPath);
		return $fileRelPath;
	}

	public function addMediaFile($dirSuffix, $fileName, $tempFile)
	{
		$fileRelPath = $this->placeMediaFile($dirSuffix, $fileName, $tempFile);
		$this->addFile($fileRelPath);
		return $fileRelPath;
	}

	private function placeMediaFile($dirSuffix, $fileName, $tempFile)
	{
		$part = sprintf('%02d', $this->count);
		$subDir = "{$this->fileNameBase}_{$dirSuffix}_{$part}";

		$mediaDir = "{$this->baseDir}/{$this->count}/{$subDir}";
		$fileRelPath = "{$subDir}/{$fileName}";
		$filePath = "{$this->baseDir}/{$this->count}/{$fileRelPath}";

		try
		{
			if (!$this->createDirectoryTree($mediaDir))
				throw new Exception("Failed to create temp directory '{$mediaDir}'");

			if (false === @rename($tempFile, $filePath))
			{
				throw new Exception ("Failed to move file; {$tempFile} to {$filePath}");
			}
		}
		catch (Exception $e)
		{
			LogError("{$this->context()}{$e}");
			throw $e;
		}

		return $fileRelPath;
	}

	private function writeMediaFileContent($dirSuffix, $fileName, $content)
	{
		$part = sprintf('%02d', $this->count);	
		$subDir = "{$this->fileNameBase}_{$dirSuffix}_{$part}";
		
		$mediaDir = "{$this->baseDir}/{$this->count}/{$subDir}";
		$fileRelPath = "{$subDir}/{$fileName}";		
		$filePath = "{$this->baseDir}/{$this->count}/{$fileRelPath}";
		$file = null;
		
		try
		{
			if (!$this->createDirectoryTree($mediaDir))
				throw new Exception("Failed to create temp directory '{$mediaDir}'");
			
			$file = fopen($filePath, 'wb');
			if ($file === false)
				throw new Exception ("Failed to create WAV file for writing: {$filePath}");
			
			$fileSize = fwrite($file, $content);
			if ($fileSize === false)
				throw new Exception ("Failed to to write to WAV file: {$fileSize}");
			
			LogMessage("{$this->context()}Successfully wrote {$fileSize} bytes to {$filePath}");
		}
		catch (Exception $e)
		{
			LogError("{$this->context()}{$e}");
			if (($file !== null) && ($file !== false))
				fclose($file);
			throw $e;
		}
		
		fclose($file);
		
		return $fileRelPath;
	}
	
	public function end()
	{
		if (($this->csvFile !== false) && ($this->csvFile !== null))
			fclose($this->csvFile);
		$this->csvFile = null;
		
		if ($this->csvFileName != null)
		{
			$this->addFile($this->csvFileName);
			$this->csvFileName = null;
		}
	}
	
	public function upload()
	{
		$this->end();
		
		for ($index=1; $index<=$this->count; ++$index) 
		{
			$part = sprintf('%02d', $index);
			$filename = "{$this->fileNameBase}_{$part}.zip";
			$zipFilePath = "{$this->baseDir}/{$index}/{$filename}";
			
			if ($this->count == 1)
				$progress = "Finalizing zip ...";
			else	
				$progress = "Finalizing {$index} of {$this->count} zips ...";	
			$this->export->app->updateExportStatus($this->export->exportID, 'Progress', $progress);			
			
			$storageKey = "export/{$this->export->orgID}/{$this->export->exportID}/{$index}";
			$this->export->uploadZipArchive($zipFilePath, $storageKey);
			
			$this->export->app->addExportDownload($this->export->exportID, $index, $filename);
		}
	}

	public function count()
	{
		return $this->count;
	}
	
	private function addFile($relFileSpec)
	{
		chdir("{$this->baseDir}/{$this->count}");
				
		$cmd = "7za a {$this->zipFileName} {$relFileSpec}";
		
		clearstatcache();
		
		LogVerbose("{$this->context()}Adding file {$relFileSpec} (".filesize($relFileSpec)." bytes) to export zip '{$this->zipFileName}' with command: ".escapeshellcmd($cmd)." -p********");
		
		$cmd .= " -p{$this->export->zipPassword}";
		
		$output = array();
		$return_var = 0;
		$zipRes = exec(escapeshellcmd($cmd), $output, $return_var);
		
		$zipFilePath = "{$this->baseDir}/{$this->count}/{$this->zipFileName}";
		if (!file_exists($zipFilePath))
		{
			LogVerbose("{$this->context()}zip result: {$zipRes}");
			LogVerbose("{$this->context()}returned: {$return_var}");
			foreach ($output as $line)
			{
				LogVerbose("{$this->context()}zip output: {$line}");
			}
			throw new Exception("Failed to create zip file '{$zipFilePath}'");
		}
		
		LogDebug("{$this->context()}zip result: {$zipRes}");
		LogDebug("{$this->context()}returned: {$return_var}");
		foreach ($output as $line)
		{
			LogDebug("{$this->context()}zip output: {$line}");
		}
		
		LogDebug("{$this->context()}Added file {$relFileSpec} to export zip '{$this->zipFileName}' (".filesize($zipFilePath)." bytes) OK");		
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
	
	private function context()
	{
		return $this->export->context();
	}
	
	private $export = null;
	private $searchStartTime = null;
	private $searchEndTime = null;
	
	private $baseDir = null;
	
	private $count = 0;
	
	private $fileNameBase = null;
	private $zipFileName = null;
	private $csvFileName = null;
	private $csvFile = null;
	
	const TEMP_DIR = '/tmp/rmarchexport';
	const TEMP_CLI_DIR = '/tmp/rmarchexport-cli';
}

class ArchivingS3Endpoints
{
	public function __construct(ArchivingExportApp $app, $context)
	{
		$this->app = $app;
		$this->context = $context;
		
		$this->details = array();
		//$this->endpoints = array();
	}

	/**
	 * @param $orgID
	 * @param $policyID
	 * @param int $policyVersion
	 * @return ArchivingEndpoint
	 * @throws Exception
	 */
	public function getDetails($orgID, $policyID, $policyVersion=0)
	{
		if (!isset($this->details[$policyID][$policyVersion]))
		{
			$policy = ArchivingPolicy::Get($this->app->api, $orgID, $policyID, $policyVersion);
			$details = ArchivingEndpoint::Get($this->app->api, $orgID, $policy->storageEndpointID, true);

			if ($details->type !== 's3')
            {
                throw new Exception('Endpoint is not on Amazon S3');
			}

			if (!isset($this->details[$policyID]))
            {
                $this->details[$policyID] = array();
			}

			$this->details[$policyID][$policyVersion] = $details;
		}

		return $this->details[$policyID][$policyVersion];
	}

	public function getBucket($orgID, $policyID, $policyVersion=0)
	{
		return $this->getDetails($orgID, $policyID, $policyVersion)->bucket;
	}

	/**
	 * @param $orgID
	 * @param $policyID
	 * @param int $policyVersion
	 * @return array(S3EndpointConfig, S3BucketConfig, S3Credentials)
	 * @throws Exception
	 */
	public function get($orgID, $policyID, $policyVersion=0)
	{
		$details = $this->getDetails($orgID, $policyID, $policyVersion);
		return array(
			new S3EndpointConfig(),
			new S3BucketConfig($details->bucket, $details->region),
			new S3Credentials($details->accessKey, $details->secretKey)
        );
	}

	private function context()
	{
		return $this->context;
	}
	
	private $app = null;
	private $context = '';
	private $details = array();
}

abstract class ArchivingExport
{
	public $app = null;
	public $exportID = null;
	public $orgID = null;
	protected $userID = null;
	protected $queryQID = null;
	protected $resultIDs = null;
	public $zipPassword = null;
	protected $reason = null;
	
	protected $encryptionKey = null;
	protected $branding = null;
	protected $policyID = null;
	protected $userDetails = null;
	protected $userTimezone = null;
	
	protected $policyPermissions = array();
	protected $policyNotificationList = array();

	/** @var ArchivingExportZip */
	protected $zip = null;

	/** @var ArchivingS3Endpoints */
	protected $s3endpoints;
	
	protected $exportedPolicyIDs = array();
	protected $exportedMediaUUIDs = array();
			
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
	
		$this->s3endpoints = new ArchivingS3Endpoints($this->app, $this->context()); 
		
		$this->exportedPolicyIDs = array();
		$this->exportedMediaUUIDs = array();
		
		LogMessage("{$this->context()}Starting ".$this->displayType()." log export: ExportID: {$this->exportID}, OrgID: {$this->orgID}, UserID: {$this->userID}, QueryQID: {$this->queryQID}, ResultIDCount: ".count($this->resultIDs) . ", Reason: {$this->reason}");
		LogDebug("{$this->context()}Password: {$this->zipPassword}");
		
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

		//echo "callLogsData:\n".print_r($callLogsData);
		
		// Get archive retention details from big DB

		$dbRetResults = $this->getLogsAndRetentionDbResults();
		
		//echo "dbRetResults:\n".print_r($dbRetResults);

		LogMessage("{$this->context()}Processing ".count($callLogsData)." ".$this->displayType()." log entries...");
		
		// set up/clean temporary directory where we will stick CSV, WAV, MMS (.eml) and zip files
		
		$this->zip = new ArchivingExportZip($this, $searchStartTime, $searchEndTime);

		$result = false;
		
		if (!empty($this->resultIDs))
			$totalCount = count($this->resultIDs); 
		else
			$totalCount = count($callLogsData);
		
		// righto off we go! for each log entry...

		$exportedCount = 0;
		$fullyExportedCount = 0;
		$lastExportedCallLogData = null;
		foreach ($callLogsData as $resID => &$callLogData)
		{
			if (empty($this->resultIDs) || isset($this->resultIDs[$resID]))
			{
				if (isset($dbRetResults[$resID]))
					$dbRetResult = $dbRetResults[$resID];
				else
					$dbRetResult = array();	
				
				try
				{
					// returns false if media was too big to download or would put export zip file size over size limit 
					if ($this->exportLogEntry($callLogData, $dbRetResult))
					{
						++$fullyExportedCount;
					}
					++$exportedCount;
					$lastExportedCallLogData = $callLogData;
					
					if ($this->exportID > 0)
					{
						$progress = "{$exportedCount} of {$totalCount} entries ...";	
						$this->app->updateExportStatus($this->exportID, 'Progress', $progress);
					}
				}
				catch (TruncateExportException $ex)
				{
					// Zip file size limit would be exceeded - truncate export before this entry
					LogWarning("{$this->context()}Zip archive would exceed {$this->app->maxZipFileSizeMB} MB file size limit - truncating export zip before result ID {$resID}", null, 'U:ARCX');
					
					$result = "OK but truncated ({$exportedCount} of {$totalCount} entries exported)";
					break;
				}
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
		
		$this->zip->upload();
	
		// Having a UUID in the export URL is just to provide a bit of obfuscation 
		// and to give the appearance that export download URLs are as secure/unguessable as recording/SMS/MMS download URLs
		$exportUUID = trim(exec('/usr/bin/uuidgen -t'));
		
		$exportURI = "export/{$this->orgID}/{$exportUUID}";
		$res = new stdClass();
		$res->policyID = $this->policyID;
		$res->exportURI = $exportURI;
		$res->result = $result;
		
		$this->writeEventLogSuccess($exportURI, $lastExportedCallLogData, $exportedCount, $totalCount);
		
		return $res;
	}
	
	abstract protected function exportLogEntry(array &$logData, array &$res);

	protected function getUserTimeZone($userID)
	{
		$xpath = API::Get("/profiles/User/{$userID}");

		$profileResponseDom = simplexml_import_dom($xpath->_dom);

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
			LogMessage("{$this->context()}Couldn't determine time zone for user ID {$userID} - defaulting to {$timezone}");
		}
		
		return new DateTimeZone($timezone);
	}

	protected function getUserDetails($userID)
	{
		$xpath = API::Get("/users/{$userID}");

		$usersResponseDom = simplexml_import_dom($xpath->_dom);

		$details = new stdClass();	
		$fields = array('UserName', 'FirstName', 'MiddleNames', 'LastName');
		foreach ($fields as $field)
		{
			$fieldNode = $usersResponseDom->xpath('User[1]/@'.$field);
			if (empty($fieldNode)) 
				throw new Exception("Failed to extract field '{$field}' from details for user ID {$userID}");
			$value = (string)$fieldNode[0];
			
			$field = strtolower($field);
			$details->{$field} = $value;
		}
		
		return $details;
	}

	abstract protected function getApiLogsPath();

	protected function getApiLogsData(&$searchStartTime, &$searchEndTime, array &$logsData)
	{
		$logsParamsXml = "<Parameters QID=\"{$this->queryQID}\" Max=\"8000\" Offset=\"0\" ShowMappings=\"YES\" UserFormat=\"first last\" ArchivingForUserID=\"{$this->userID}\"/>";
		$xpath = API::Post($this->getApiLogsPath(), $logsParamsXml, array(200, 201), true, array('Content-Type'  => 'application/xml'));

		$logsResponseDom = simplexml_import_dom($xpath->_dom);

		$searchStartTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[1]/@Val');
		$searchEndTimeNodes = $logsResponseDom->xpath('Criteria[1]/And[1]/Condition[2]/@Val');
		if (empty($searchStartTimeNodes) || empty($searchEndTimeNodes))
			throw new Exception("Failed to fetch logs search criteria for query QID {$this->queryQID}");
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
		$xpath = API::Get("/branding/{$this->orgID}");

		$brandingDom = simplexml_import_dom($xpath->_dom);

		$nodes = $brandingDom->xpath('@Identifier');
		if (empty($nodes))
			throw new Exception("Failed to extract 'Identifier' attribute from CoreAPI branding settings for org ID {$this->orgID}");
		
		$identifier = (string)$nodes[0];
		if (empty($identifier))
			throw new Exception("Failed to extract 'Identifier' attribute from CoreAPI branding settings for org ID {$this->orgID}");
		
		$branding = Brand::ByIdentifier($identifier);
		if (!is_object($branding))
			throw new Exception("Failed to find branding settings for '{$identifier}' for org ID {$this->orgID}");
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
	
	protected function getArchivingPolicyPermission($policyID, $ownerUserID)
	{
		LogVerbose("{$this->context()}Checking CoreAPI archiving policy permission for policy ID {$policyID}, Retention User ID {$ownerUserID}");
		
		if (!isset($this->policyPermissions[$policyID]))
		{
			$this->policyPermissions[$policyID] = array();
		}
		else if (isset($this->policyPermissions[$policyID][$ownerUserID]))
		{
			$perm = $this->policyPermissions[$policyID][$ownerUserID];
			LogDebug("{$this->context()}Using previously cached permission result: ".($perm->allowed?'true':'false'));
			return $perm;
		}

		$xpath = API::Get("/archiving/policypermissions/{$this->orgID}/{$policyID}/{$this->userID}/{$ownerUserID}/read");
		
		$permsDom = simplexml_import_dom($xpath->_dom);

		$nodes = $permsDom->xpath('@Allowed');
		if (empty($nodes))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
		$allowed = (string)$nodes[0];
		$allowed = ($allowed == 'YES');
						
		$nodes = $permsDom->xpath('@RetentionMax');
		if (empty($nodes))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
		$retentionMax = (int)$nodes[0];
		
		$nodes = $permsDom->xpath('@RetentionMaxUnit');
		if (empty($nodes))
			throw new Exception("Failed to query CoreAPI archiving policy permissions for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");
		$retentionMaxUnit = (string)$nodes[0];	
		
		LogDebug("{$this->context()}Checked CoreAPI archiving policy permission for policy ID {$policyID}, Retention User ID {$ownerUserID}, response: {$permsResponse}");	

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
		
		$perm = new stdClass();
		$perm->allowed = $allowed;
		$perm->retentionMax = $retentionMax;
		$perm->retentionMaxUnit = $retentionMaxUnit;
		
		// cache result
		$this->policyPermissions[$policyID][$ownerUserID] = $perm;
		
		return $perm;
	}
	
	protected function checkRetentionMaxTime($perm, $creationDate)	
	{
		$datetime = new DateTime('@'.$creationDate, new DateTimeZone('UTC'));
		// TODO we may need to use the Org's home timezone to make more accurate calculations; otherwise DST (kinda) introduces error!
		// 2014-10-01 10:10:10 + 1 month = 2014-11-01 09:10:10
		// $datetime->setTimezone(new DateTimeZone('Europe/London'));
		$offsetDatetime = $datetime->add(
			DateInterval::createFromDateString(
				sprintf('%d %s', $perm->retentionMax, $perm->retentionMaxUnit)
			)
		);
		if (false === $offsetDatetime)
			throw new Exception("Failed to calculate time-offset");
		
		return $offsetDatetime->getTimestamp() >= time();
	}	
	
	protected function getS3ObjectContent($policyID, $policyVersion, $storageKey, &$decryptedFilePath)
	{
		// Get endpoint details from API
		list($s3endpoint, $bucket, $s3creds) = $this->s3endpoints->get($this->orgID, $policyID, $policyVersion);
		
		// First check if size not too big
		
		LogVerbose("{$this->context()}Getting S3 object info {$storageKey} from S3 Bucket {$bucket}");
		
		$infoResponse = S3::getObjectInfo($bucket, $storageKey, true, $s3endpoint, $s3creds);
		if (empty($infoResponse) || empty($infoResponse['x-amz-meta-clear-size']) || empty($infoResponse['size']))
			throw new Exception("Failed to fetch {$storageKey} S3 object info");
	
		foreach ($infoResponse as $h => $v)
			LogDebug("{$this->context()}header: '{$h}' = '{$v}'");
			
		$filesize = (double)$infoResponse['x-amz-meta-clear-size'];
		$s3CompressedFilesize = (double)$infoResponse['size'];
		
		LogVerbose("{$this->context()}Uncompressed object size (x-amz-meta-clear-size): {$filesize}, S3 compressed object size: {$s3CompressedFilesize}");
		
		if (!$this->zip->canAddMediaFile($filesize, $s3CompressedFilesize))
			// skip this exporting this entry, but keep going (as file media of subsequent entries may still fit into the export zip
			return $filesize;
		
		// size OK - download object from S3
		LogVerbose("{$this->context()}Downloading {$storageKey} from S3 Bucket {$bucket}");

		$tempDir = $this->app->cliMode ? ArchivingExportZip::TEMP_CLI_DIR : ArchivingExportZip::TEMP_DIR;

		$s3File = new TempPath("{$storageKey}_s3", "{$tempDir}/{$this->exportID}");
		$s3FileFp = @fopen($s3File->getPath(), 'wb');
		if (false === $s3FileFp)
		{
			throw new Exception("Unable to open temporary file {$s3File}");
		}

		/** @noinspection PhpUnusedLocalVariableInspection */
		/** @noinspection OnlyWritesOnParameterInspection */
		$tmpFpSentry = new ScopeSentry(function () use ($s3FileFp) {
			if (is_resource($s3FileFp)) {
				@fclose($s3FileFp);
			}
		});

		$response = S3::getObject($bucket, $storageKey, $s3FileFp, $s3endpoint, $s3creds);
		if (!isset($response->code) || $response->code !== 200)
		{
			$message = "There was a problem downloading {$storageKey} from S3";
			if (isset($response->error))
				$message .= ": {$response->error}";
			throw new Exception($message);
		}

		if (!isset($response->headers) || !is_array($response->headers))
			throw new Exception("{$storageKey} was successfully downloaded from S3 but had no headers; skipping");

		$s3FileSize = filesize($s3File);
		if ($s3FileSize === 0)
			throw new Exception("{$storageKey} GET was successful but no body was returned");

		LogVerbose("{$this->context()}Successfully downloaded {$storageKey} from S3 Bucket {$bucket}");
		
		try
		{
			$decryptedFile = new TempPath($storageKey, "{$tempDir}/{$this->exportID}");

			LogVerbose("{$this->context()}Processing downloaded file content of {$storageKey}, size {$s3FileSize} bytes");
			ArchivingProcessFile::ProcessFile($s3File, $decryptedFile, $this->encryptionKey);
			if ($filesize != filesize($decryptedFile))
			{
				throw new Exception("{$storageKey} Size of the decrypted file did not match the value from header");
			}

			$decryptedFile->setRetain();
			$decryptedFilePath = $decryptedFile->getPath();
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

	public function uploadZipArchive($zipFilePath, $storageKey)
	{
		list($s3endpoint, $bucket, $s3creds) = $this->s3endpoints->get($this->orgID, $this->policyID);
		
		// OK REALLY REALLY important to call clearstatcache() first as filesize() is called internally by S3::putObjectFile() and:
		// 
		// 'On Linux based systems, data fetched by filesize() is "statcached".
		// Try calling clearstatcache(); before the filesize call.'
		// http://stackoverflow.com/questions/3747982/php-filesize-reporting-old-size
		// 
		clearstatcache();
		
		LogVerbose("{$this->context()}Uploading export zip {$storageKey} (".filesize($zipFilePath)." bytes) to bucket {$bucket} in S3...");
			
		if (!S3::putObjectFile($zipFilePath, $bucket, $storageKey, S3::ACL_PRIVATE, array(), null,
			S3::STORAGE_CLASS_STANDARD, S3::SSE_NONE, $s3endpoint, $s3creds))
		{
			throw new Exception("Failed to store file '{$zipFilePath}' in S3 bucket: {$bucket}, storageKey: {$storageKey}");
		}

		LogVerbose("{$this->context()}Uploaded export zip to S3 OK");
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
	
	protected function writeEventLogSuccess($exportURI, $lastExportedLogData, $exportedCount, $totalCount)
	{
		$fileservice = $this->getBrandingSetting('URL/FileService', 'https://fileservice.natterbox.com');
		
		if ($exportedCount == 1)
			$exported = "1 exported entry";
		else
			$exported = "{$exportedCount} exported entries";
		
		if ($this->zip->count() > 1)
		{
			$completedMsg = "Your {$this->displayType()} Logs Bulk Download can be accessed here ({$exported}):";
			$completedMsg .= "<ul>";
			for ($index=1; $index<=$this->zip->count(); ++$index)
				$completedMsg .= "<li><a href=\"{$fileservice}/{$exportURI}/{$index}\">part {$index}</a></li>";
			$completedMsg .= "</ul>";
			$completedMsg .= "These download links will only be accessible for 7 days.<br/>";
		}
		else
		{
			$completedMsg = "Your {$this->displayType()} Logs Bulk Download can be accessed <a href=\"{$fileservice}/{$exportURI}/1\">here</a> ({$exported}).<br/>";
			$completedMsg .= "<br/>This download link will only be accessible for 7 days.<br/>";
		}
		
		if ($exportedCount < $totalCount)
		{
			$completedMsg .= "<br/>Note this bulk download was truncated after {$exportedCount} of {$totalCount} total entries due to size limits.<br/>";	
			if (is_array($lastExportedLogData))
			{
				$time = (int)$lastExportedLogData['Time'];
				$fromNumber = $lastExportedLogData['From_Number'];
				$toNumber  = $lastExportedLogData['To_Number'];
				$startTimeLocal = formatTime($time, $this->userTimezone, 'Y-m-d H:i:s');
				$completedMsg .= "Truncated after entry: Date/Time {$startTimeLocal}, From {$fromNumber}, To {$toNumber}.<br/>";
			}
		}
		
		$notify = array();
		$notify[] = array(
			'Type' => 'Email',
			'Subject' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} is complete",
			'Heading' => "{$this->displayType()} Logs Bulk Download ID {$this->exportID} is complete",
			'To' => array($this->userID),
			'From' => array(
				$this->getBrandingSetting('Common/NameSimplified', 'Natterbox').' Archive',
				$this->getBrandingSetting('Emailer/FromEmailAddress', 'noreply@natterbox.com')),
			'Text' => $completedMsg
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
		
		// disabled for now as needs API change to support being invoked by daemon PLAT-2911
		/*
		// don't really want to fail the whole job if portal notification fails to send - portal notification is just a nice to have	
		try
		{
			$this->writePortalNotification();
		}
		catch (Exception $ex) 
		{
			LogWarning("{$this->context()}Failed to send portal notification: {$ex}", null, 'U:ARCX');
		}
		*/
	}
		
	private function writePortalNotification()
	{
		$subject = "{$this->displayType()} Logs Bulk Download ID {$this->exportID} is complete";
		$body = "Your {$this->displayType()} Logs Bulk Download can be accessed in the Logs 'Bulk Downloads' tab'.";

		$notifyParamsXml = "<Parameters Subject=\"{$subject}\" Body=\"{$body}\" Priority=\"1\" Mode=\"Portal\" UserList=\"{$this->userID}\"/>";

		API::Put('/notifications', $notifyParamsXml, array(200, 201, 202), true, array('Content-Type'  => 'application/xml'));
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
	
	abstract public function getCsvHeaderFields();
	
	public function context()
	{
		return "{$this->app->context()}ExportID:{$this->exportID}: ";
	}	
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
		$resID = (int)$logData['ResID'];
		$startTimeUTC = gmstrftime('%F %T', $time);
		
		LogVerbose("{$this->context()}Processing logs result ID {$resID}, AUUID: {$AUUID}, Recorded: '{$recorded}'...");
		
		$success = true;
		
		$recording = '"None"';
		$recordingTime = '';
		$recordingSize = '';
		if (!empty($res) && ($recorded == 'YES'))
		{
			$prefix = formatTime($time, $this->userTimezone, 'Y-m-d@H.i.s') . "_from-{$fromNumber}_to-{$toNumber}";
			$archRec = $this->getArchivedRecording($logData, $res, $prefix);
			if (is_object($archRec))
			{
				LogVerbose("{$this->context()}Fetched recording for result ID {$resID}: {$archRec->filepath}");
				
				$recording = "=HYPERLINK(\"{$archRec->filepath}\")";
				$recordingSize = $archRec->filesize;
				$recordingSecs = (int)((double)$recordingSize / 16384.0); // 16-bit WAV
				$recordingTime = strftime('%T', $recordingSecs);
				
				$this->addAuditDetails($archRec, $startTimeUTC);
			}
			else if (is_double($archRec))
			{
				LogMessage("{$this->context()}Recording result ID {$resID} (AUUID '{$AUUID}') was too big to export: {$archRec} bytes - skipping recording");
				
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
		
				LogVerbose("{$this->context()}No recording for result ID {$resID}, AUUID {$AUUID}, reason '{$archRec}' - skipping recording");
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
			$recording,					// Recording
			"\"{$recordingTime}\"",		// Recording Time
			"=\"{$recordingSize}\"",	// Recording Size (Bytes)	- leading '=' prevents Excel from displaying as float/double		
			"=\"{$startTimeLocal}\"",	// Date / Time				- leading '=' prevents Excel from displaying date/time in locale format		
			"=\"{$startTimeUTC}\"",		// UTC Date / Time			- leading '=' prevents Excel from displaying date/time in locale format		
			"=\"{$endTimeLocal}\"",		// End Date / Time			- leading '=' prevents Excel from displaying date/time in locale format		
			"\"{$totalCallTime}\"",		// Total Call Time
			"\"{$totalCallTimeSecs}\"",	// Total Call Time (INT)
			"\"{$from}\"",				// From
			"\"{$fromType}\"",			// From Type
			"\"{$fromLabel}\"",			// From Label
			"=\"{$fromNumber}\"",		// From (Number)			- leading '=' prevents Excel from displaying as float/double
			"\"{$fromDev}\"",			// From (Device)
			"=\"{$toNumberDialled}\"", 	// Dialled (Number)			- leading '=' prevents Excel from displaying as float/double
			"\"{$toUser}\"", 			// Connected To
			"\"{$toType}\"", 			// To Type
			"\"{$toLabel}\"", 			// To Label
			"=\"{$toNumber}\"",			// Connected To (Number)	- leading '=' prevents Excel from displaying as float/double
			"\"{$toDev}\"", 			// Connected To (Device)
			"\"{$timeHunting}\"",		// Time Hunting
			"\"{$timeRinging}\"",		// Time Ringing
			"\"{$timeTalking}\"",		// Time Talking
			"\"{$timeHuntingSecs}\"",	// Time Hunting (INT)
			"\"{$timeRingingSecs}\"",	// Time Ringing (INT)
			"\"{$timeTalkingSecs}\"",	// Time Talking (INT)
			"\"{$direction}\"",			// Direction
			"\"{$fromHangup}\"",		// From HangUp
			"\"{$toHangup}\"",			// To HangUp
			"\"{$type}\"", 				// Type
			"\"{$flags}\"",				// Flags
			"\"{$policy}\"",			// Policy
			"\"{$AUUID}\"",				// AUUID
			"\"{$BUUID}\"",				// BUUID
			"\"{$transfer}\"",			// Transfer
			"\"{$attTransfer}\"",		// Att Transfer
			"\"{$blindTransfer}\"",		// Blind Transfer
			"\"{$cliWithheld}\"", 		// CLI Withheld
			"\"{$alert}\"", 			// Alert
			"\"{$callPickedUp}\"",		// Call Picked Up
			"\"{$intercepted}\"",		// Intercepted
			"\"{$callQueue}\"",			// Call Queue
			"\"{$eavesdropAttached}\"",	// Eavesdrop Attached
			"\"{$eavesdrop}\"",			// Eavesdrop
			"\"{$recorded}\"",			// Recorded
			"\"{$earlyDivert}\"",		// Early Divert
			"\"{$lateDivert}\"",		// Late Divert
			"\"{$voiceMail}\"",			// Voice Mail
			"\"{$diverted}\"",			// Diverted
			"\"{$listenIn}\"",			// Listen In,
			"\"{$activeRecOK}\"", 		// Active Rec OK
			"\"{$activeRecFail}\"",		// Active Rec Fail
			"\"{$activeRecBufOK}\"", 	// Active Rec Buffered OK
			"\"{$activeRecBufFail}\"",	// Active Rec Buffered Fail
			"\"{$pcap}\""				// PCAP
		);
		$this->zip->writeCSVEntryFields($csvEntryFields);
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
		$sql= "SELECT \n" .
			"LOWER(HEX(l.`AUUID`)) AS `HexAUUID`, \n" .
			"LOWER(HEX(l.`BUUID`)) AS `HexBUUID`, \n" .
			"l.`ResID`, \n" .
			"ra.`LegUUID` AS `RetentionALegUUID`, \n" .
			"ra.`UserID` AS `RetentionALegUserID`, \n" .
			"ra.`PolicyID` AS `RetentionALegPolicyID`, \n" .
			"ra.`PolicyVersion` AS `RetentionALegPolicyVersion`, \n" . 
			"UNIX_TIMESTAMP(ra.`CreationDate`)+ra.`MidnightOffsetSeconds` AS `RetentionALegCreationDate`, \n" .
			"ra.`State` AS `RetentionALegState`, \n" . 
			"ra.`Purged` AS `RetentionALegPurged`, \n" . 
			"rb.`LegUUID` AS `RetentionBLegUUID`, \n" .
			"rb.`UserID` AS `RetentionBLegUserID`, \n" .
			"rb.`PolicyID` AS `RetentionBLegPolicyID`, \n" .
			"rb.`PolicyVersion` AS `RetentionBLegPolicyVersion`, \n" . 
			"UNIX_TIMESTAMP(rb.`CreationDate`)+rb.`MidnightOffsetSeconds` AS `RetentionBLegCreationDate`, \n" .
			"rb.`State` AS `RetentionBLegState`, \n" . 
			"rb.`Purged` AS `RetentionBLegPurged` \n" .
			"FROM `Org_{$this->orgID}`.`Results_Logs_{$this->userID}_{$this->queryQID}` l \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` ra ON (l.`AUUID`=ra.`LegUUID`) \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` rb ON (l.`BUUID`=rb.`LegUUID`) \n" .
			"WHERE (ra.Type=".Archiving::FILE_TYPE_INT_RECORDING." OR rb.Type=".Archiving::FILE_TYPE_INT_RECORDING.");";
								
		$dbResults = $this->app->bigDbc->query($sql)->fetch_all(MySQLiResult::AS_ASSOC);
		
		$results = array();
		foreach ($dbResults as &$dbResult)
		{
			$resID = (int)$dbResult['ResID'];
			$results[$resID] = $dbResult;
		}
		return $results;
	}
	
	private function getArchivedRecording(array $logData, array $res, $prefix)
	{
		$resID = $logData['ResID'];
		
		$uuid = false;
		$policyVersion = false;
		$policyID = false;
		$ownerUserID = false;
		$purged = false;
		$creationDate = false;
		
		//echo "logData:\n".print_r($logData);
		//echo "res:\n".print_r($res);

		if (!empty($res['RetentionALegUUID']))
		{
			$uuid = $logData['AUUID'];
			$policyVersion = (int)$res['RetentionALegPolicyVersion'];
			$policyID = (int)$res['RetentionALegPolicyID'];
			$ownerUserID = (int)$res['RetentionALegUserID'];
			$creationDate = (int)$res['RetentionALegCreationDate'];
			
			if ($res['RetentionALegState'] == 'Available')
			{
				$purged = !($res['RetentionALegPurged'] == 'no');	
				if ($purged) 
				{
					LogVerbose("{$this->context()}Processing result ID {$resID}: call is recorded but retention entry for ALeg UUID '{$logData['AUUID']}' has been purged");
				}
			}
			else
			{
				LogVerbose("{$this->context()}Processing result ID {$resID}: call is recorded but retention entry for ALeg UUID '{$logData['AUUID']}' not available: State: {$res['RetentionALegState']}, Purged: {$res['RetentionALegPurged']}");
			}
		}
		else if (!empty($res['RetentionBLegUUID'])) 
		{
			$uuid = $logData['BUUID'];
			$policyVersion = (int)$res['RetentionBLegPolicyVersion'];
			$policyID = (int)$res['RetentionBLegPolicyID'];
			$ownerUserID = (int)$res['RetentionBLegUserID'];
			$creationDate = (int)$res['RetentionBLegCreationDate'];
			
			if ($res['RetentionBLegState'] == 'Available')
			{
				$purged = !($res['RetentionBLegPurged'] == 'no');
				if ($purged)
				{
					LogVerbose("{$this->context()}Processing result ID {$resID}: call is recorded but retention entry for BLeg UUID '{$logData['BUUID']}' has been purged");
				}
			}
			else
			{
				LogVerbose("{$this->context()}Processing result ID {$resID}: call is recorded but retention entry for BLeg UUID '{$logData['BUUID']}' not available: State: {$res['RetentionBLegState']}, Purged: {$res['RetentionBLegPurged']}");
			}
		}
		else
		{
			LogWarning("{$this->context()}Processing result ID {$resID}: call is recorded but no retention entry found for ALeg UUID '{$logData['AUUID']}' or BLeg UUID '{$logData['BUUID']}'", null, 'U:ARCX');
		}
		
		if (!$uuid || $policyID == 0 || $creationDate == 0) 
			// shouldn't happen
			return 'Not Available';
		
		$perm = $this->getArchivingPolicyPermission($policyID, $ownerUserID);
		if (!isset($perm) || !isset($perm->allowed) || !$perm->allowed)
			return 'Access Denied';
		
		if ($purged || !$this->checkRetentionMaxTime($perm, $creationDate))
			return 'Purged';
						
		// Construct storage key from UUID
		$storageKey = "recording/{$this->orgID}/{$uuid}";

		$decryptedFilePath = null;
		$contentSize = $this->getS3ObjectContent($policyID, $policyVersion, $storageKey, $decryptedFilePath);
		if (($contentSize > 0) && empty($decryptedFilePath))
			// recording too big
			return $contentSize;
		
		$wavFileRelPath = $this->zip->addMediaFile('recordings', "{$prefix}_{$uuid}.wav", $decryptedFilePath);
		
		$recording = new stdClass();
		$recording->filepath = $wavFileRelPath;
		$recording->filesize = $contentSize;
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

	public function getCsvHeaderFields()
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
		$resID = (int)$logData['ResID'];
		
		$timeUTC = gmstrftime('%F %T', $time);

		LogVerbose("{$this->context()}Processing SMS/MMS logs result ID {$resID}, UUID: {$UUID}...");
		
		$success = true;
		
		$messageSize = '';
		
		$prefix = formatTime($time, $this->userTimezone, 'Y-m-d@H.i.s') . "_from-{$fromNumber}_to-{$toNumber}";
		
		if (!empty($res))
			$archMsg = $this->getArchivedMessage($logData, $res, $prefix);
		else
			$archMsg = null;
		
		if (isset($archMsg->filepath))
		{
			LogVerbose("{$this->context()}Fetched MMS for result ID {$resID}: {$archMsg->filepath}");
			
			$message = "=HYPERLINK(\"{$archMsg->filepath}\")";
			$messageSize = $archMsg->filesize;
			
			$this->addAuditDetails($archMsg, $timeUTC);
		}
		else if (isset($archMsg->text))
		{
			LogDebug("{$this->context()}Fetched SMS for result ID {$resID}");
			
			$text = str_replace("\"", "\"\"", $archMsg->text);
			$message = "\"{$text}\"";
			$messageSize = strlen($archMsg->text);

			$this->addAuditDetails($archMsg, $timeUTC);
		}
		else if (is_double($archMsg))
		{
			LogMessage("{$this->context()}SMS/MMS result ID {$resID} (UUID '{$UUID}') was too big to export: {$archMsg} bytes - skipping message");
			
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
	
			LogVerbose("{$this->context()}No SMS/MMS message for result ID {$resID}, UUID {$UUID}, reason '{$archMsg}' - skipping message");
			$message = "\"{$archMsg}\"";
		}
		
		$timeLocal = formatTime($time, $this->userTimezone, 'Y-m-d H:i:s');
		
		$csvEntryFields = array
		(
			$message,				// Message
			"=\"{$messageSize}\"",	// Message Size		- leading '=' prevents Excel from displaying as float/double
			"=\"{$timeLocal}\"",	// Date / Time		- leading '=' prevents Excel from displaying date/time in locale format
			"=\"{$timeUTC}\"",		// UTC Date / Time	- leading '=' prevents Excel from displaying date/time in locale format
			"\"{$user}\"",			// User
			"=\"{$fromNumber}\"",	// From (Number) 	- leading '=' prevents Excel from displaying as float/double
			"=\"{$toNumber}\"", 	// To (Number)	 	- leading '=' prevents Excel from displaying as float/double
			"\"{$direction}\"",		// Direction
			"\"{$UUID}\"",			// UUID
		);
		$this->zip->writeCSVEntryFields($csvEntryFields);
		
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
		$creationDate = false;

		if (!empty($res['RetentionUUID']))
		{
			$uuid = $logData['UUID'];
			$policyVersion = (int)$res['RetentionPolicyVersion'];
			$policyID = (int)$res['RetentionPolicyID'];
			$ownerUserID = (int)$res['RetentionUserID'];
			$creationDate = (int)$res['RetentionCreationDate'];
			
			if ($res['RetentionState'] == 'Available') 
			{
				$purged = !($res['RetentionPurged'] == 'no');
				if ($purged) 
				{
					LogVerbose("{$this->context()}Processing result ID {$resID}: retention entry for UUID '{$logData['UUID']}' is purged");
				}
			}
			else
			{
				LogVerbose("{$this->context()}Processing result ID {$resID}: retention entry for UUID '{$logData['UUID']}' not available: State: {$res['RetentionState']}, Purged: {$res['RetentionPurged']}");
			}
		}
		else
		{
			LogWarning("{$this->context()}Processing result ID {$resID}: no retention entry found for UUID '{$logData['UUID']}'", null, 'U:ARCX');
		}
		
		if (!$uuid || $policyID == 0 || $creationDate == 0) 
			// shouldn't happen
			return 'Not Available';
		
		$perm = $this->getArchivingPolicyPermission($policyID, $ownerUserID);
		if (!isset($perm) || !isset($perm->allowed) || !$perm->allowed)
			return 'Access Denied';
				
		if ($purged || !$this->checkRetentionMaxTime($perm, $creationDate))
			return 'Purged';
		
		// Construct storage key from UUID
		$storageKey = "sms/{$this->orgID}/{$uuid}";
		
		// a big MMS probably already contains compressed media (mp4 etc), so we don't expect to get further compression 
		// - hence specifying expected p7zip compression ratio of 1.0 (i.e. no compression)
		$decryptedFilePath = null;
		$contentSize = $this->getS3ObjectContent($policyID, $policyVersion, $storageKey, $decryptedFilePath);
		if (($contentSize > 0) && empty($decryptedFilePath))
			// recording too big
			return $contentSize;
		
		$message = new stdClass();
		$message->uuid = $uuid;
		$message->policyID = $policyID;

		// seems crude, but this is exactly how the portal determines if message is SMS or MMS
		if (file_get_contents($decryptedFilePath, false, null, 0, 5) === '<?xml')
		{
			// SMS message - extract text field from XML

			$content = file_get_contents($decryptedFilePath);

			//LogDebug("SMS XML: {$content}");
			
			$smsDom = simplexml_load_string($content);
			if (!($smsDom instanceof SimpleXMLElement)) 
				throw new Exception("Failed to parse SMS XML details");
			
			$textNode = $smsDom->xpath('Text[1]');
			if (empty($textNode)) 
				throw new Exception("Failed to extract text from SMS XML details");
			$text = (string)$textNode[0];
			
			$message->text = trim(html_entity_decode($text, ENT_COMPAT, 'UTF-8'));
		} 
		else
		{
			// assume this is a MMS message - write contents to .eml file
			$mmsFileRelPath = $this->zip->addMediaFile('mms', "{$prefix}_{$uuid}.eml", $decryptedFilePath);

			$message->filepath = $mmsFileRelPath;
			$message->filesize = $contentSize; 
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
		$sql = "SELECT \n" .
			"LOWER(HEX(l.`UUID`)) AS `HexUUID`, \n" .
			"l.`ResID`, \n" .
			"r.`UUID` AS `RetentionUUID`, \n" .
			"r.`UserID` AS `RetentionUserID`, \n" .
			"r.`PolicyID` AS `RetentionPolicyID`, \n" .
			"r.`PolicyVersion` AS `RetentionPolicyVersion`, \n" .
			"UNIX_TIMESTAMP(r.`CreationDate`)+r.`MidnightOffsetSeconds` AS `RetentionCreationDate`, \n" .
			"r.`State` AS `RetentionState`, \n" . 
			"r.`Purged` AS `RetentionPurged` \n" . 
			"FROM `Org_{$this->orgID}`.`Results_SMSLogs_{$this->userID}_{$this->queryQID}` l \n" .
			"LEFT JOIN `ArchivingRetention`.`Retention_{$this->orgID}` r ON (l.`UUID`=r.`UUID`) \n" .
			"WHERE (r.Type=".Archiving::FILE_TYPE_INT_SMS.");";
				
		$dbResults = $this->app->bigDbc->query($sql)->fetch_all(MySQLiResult::AS_ASSOC);
		
		$results = array();
		foreach ($dbResults as &$dbResult)
		{
			$resID = (int)$dbResult['ResID'];
			$results[$resID] = $dbResult;
		}
		return $results;		
	}
	
	public function getCsvHeaderFields()
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
	
	public $coreApiUrl = null;
	public $coreApiAuthCreds = null;
	public $api = null;

	public $coreDbc = null;
	public $bigDbc = null;
	
	public $startup = true;
	
	public $hAPISearchDomain = null;
	private $hAPILocName = null;
	
	private $maxSpawningTimeoutSecs = null;
	private $maxProcessingTimeoutHours = null;
	
	private $passwordAESKey = null;
	
	public $maxZipFileSizeMB = null;
	public $maxZipFileCount = null;
	
	public $cliMode = false;
	public $worker = false;
	
	public $maxRetries = 2;

	/** @var ArchivingS3Endpoints */
	private $s3endpoints;
	
	public $backminutes = 0;
	
	public $maxPurgeRetries = 10;
	public $maxPurgeItems = 10;
		
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

		$this->coreApiAuthCreds = $this->conf->get('coreapi.authorization_creds', false);
		if (empty($this->coreApiAuthCreds))
			throw new Exception('CoreAPI auth creds not configured');
		
		if ($hAPIHost->InProduction())
			$this->passwordAESKey = $this->conf->get('coreapi.logsexportq.aeskey', false);
		else
			$this->passwordAESKey = 'UNSECURE';
		if (empty($this->passwordAESKey))
			throw new Exception('LogExportQ AESKey not configured');
		$this->passwordAESKey = sha1($this->passwordAESKey);
		
		$this->maxSpawningTimeoutSecs = (int)$this->conf->get('export.maxspawningtimeoutsecs', 60);
		$this->maxProcessingTimeoutHours = (int)$this->conf->get('export.maxprocessingtimeouthours', 8);
		
		$this->maxWorkers = (int)$this->conf->get('export.maxworkers', 4);
		
		$this->maxFileSizeMB = (double)$this->conf->get('export.maxfilesizemb', 3000.0);
		$this->maxZipFileSizeMB = (double)$this->conf->get('export.maxzipfilesizemb', 500.0);
		$this->maxZipFileCount = (int)$this->conf->get('export.maxzipfilecount', 10);
		$this->maxRetries = (int)$this->conf->get('export.maxretries', 2);
		
		$this->maxPurgeRetries = (int)$this->conf->get('export.maxpurgeretries', 10);
		$this->maxPurgeItems = (int)$this->conf->get('export.maxpurgeitems', 10);
		
		// normally specified as days, but can specify as minutes for QA testing purposes
		$this->backminutes = (int)$this->conf->get('export.backminutes', 0);
		if ($this->backminutes == 0)
		{
			$this->backminutes = ((int)$this->conf->get('export.backdays', 7)) * 24 * 60;
		}	
		
		$this->coreApiUrl = $this->conf->get('coreapi.url', 'http://cdr.coreapi.{hapi:SearchDomain}');
		if (empty($this->coreApiUrl))
			throw new Exception('CoreAPI URL not configured');
		
		$this->cliMode = cli::get('--no-daemon', false);
		$this->worker = cli::get('--worker', false);
		
		$authorizationSentry = null;
		$requests = array();
		try
		{
			// Get Big SQL database connection
			$this->bigDbc = MySQLiConnection::Get('retention_rw');
			$this->bigDbc->query("SET time_zone = '+00:00'"); 
			
			if ($this->worker)
			{
				$exportID = cli::get('--exportid', 0);
				if (empty($exportID))
					throw new Exception('No export ID specified');
				
				$requests = $this->getExportRequest($exportID);
			}
			else 
			{
				if ($this->startup)
				{
					// first time in cleanup any requests in spawning/processing state
					$this->startup = false;
					$this->startupCleanupExportStatus();
				}
				
				if ($this->cliMode)
				{
					$requests = $this->getPendingExportRequests();
				}
				else // daemon worker monitor
				{
					$requests = $this->getPendingExportRequestIDs();
				}
			}
			
			if (empty($requests)) 
			{
				LogVerbose("{$this->context()}No pending archiving export requests to service");
				
				// if maxPurgeItems set to 0 this means purge is disabled
				if (!$this->worker && ($this->maxPurgeItems > 0))
				{
					$this->purge();
				}
					
				return 0;
			}
			
			$this->loginToCoreApi();
			
			EventLog::Configure($this->coreApiUrl);
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
			LogWarning("{$this->context()}Failed to run archiving exporter: Exception: {$e}");	
			return 1;
		}
		
		if ($this->worker || $this->cliMode)
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
					
					LogMessage("{$this->context()}Successfully exported {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: ExportURI: {$result->exportURI}");
					
					$this->updateExportStatus($exportID, 'Complete', $result);
				}
				catch (Exception $e)
				{
					$status = $this->updateExportStatus($exportID, 'Failed', $serviceFailedCount);
					if ($status == 'Failed')
					{
						LogWarning("{$this->context()}Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID}: Exception: {$e}", null, 'U:ARCX');
						if (is_object($export))
							$export->writeEventLogFailed();
					}
					else // we will retry in a bit
					{
						LogMessage("{$this->context()}Failed to export {$displayType} logs: ExportID: {$exportID}, OrgID: {$orgID}, UserID: {$userID}, QueryQID: {$queryQID} - retrying again in a bit... (Exception: {$e})");
					}
				}
				unset($export);
			}
		}
		else
		{
			// daemon mode - worker monitor: kick off worker processes
			
			foreach ($requests as $request)
			{
				$exportID = (int)$request['ExportID'];
				
				LogMessage("{$this->context()}Starting worker process for pending export ID {$exportID}");
				
				$this->updateExportStatus($exportID, 'Spawning');

				$cmd = "/usr/bin/archiving-export --worker --exportid={$exportID} >/dev/null &";
				exec($cmd);	
			}
		}
		
		return 0;
	}
	
	private function loginToCoreApi()
	{
		API::Init($this->coreApiUrl, new ArrayStorage(), APP_NAME_.':1.0');
		
		API::LoginBasicAuth($this->coreApiAuthCreds, null/*mfa*/, true/*re_authenticate*/);
		
		LogDebug("{$this->context()}Got auth CoreAPI token: ".API::GetToken());
		
		$this->api = array($this->coreApiUrl, API::GetToken());
	}	
	
	private function getPendingExportRequests()
	{
		$this->cleanupExportStatus();
		
		$processingCount = $this->getProcessingExportRequestCount();
		if ($processingCount >= $this->maxWorkers)
		{
			LogVerbose("{$this->context()}Not fetching pending requests - already processing {$processingCount} requests (limit {$this->maxWorkers})");
			return null;
		}
		
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		$sql = "SELECT `ExportID`, ".
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
			"WHERE `Status`='Pending' ". 
			"AND `ClientLocation`='{$locName}' ".
			"AND (`NextServiceAttempt` IS NULL OR UTC_TIMESTAMP() >= `NextServiceAttempt`) ".
			"ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT ".($this->maxWorkers - $processingCount).";";
		
		return $this->bigDbc->query($sql)->fetch_all(MySQLiResult::AS_ASSOC);
	}
	
	private function getPendingExportRequestIDs()
	{
		$this->cleanupExportStatus();
		
		$processingCount = $this->getProcessingExportRequestCount();
		if ($processingCount >= $this->maxWorkers)
		{
			LogVerbose("{$this->context()}Not fetching pending requests - already processing {$processingCount} requests (limit {$this->maxWorkers})");
			return null;
		}
		
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		$result = $this->bigDbc->query(
			"SELECT `ExportID` ".
			"FROM `Aux`.`LogsExportQ` ".
			"WHERE `Status`='Pending' ". 
			"AND `ClientLocation`='{$locName}' ".
			"AND (`NextServiceAttempt` IS NULL OR UTC_TIMESTAMP() >= `NextServiceAttempt`) ".
			"ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT ".($this->maxWorkers - $processingCount).";"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		return $result;	
	}
	
	private function getExportRequest($exportID)
	{
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
			"WHERE `ExportID`='{$exportID}' ".
			"AND `Status`='Spawning' ". 
			"AND `ClientLocation`='{$locName}';"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		return $result;
	}
	
	private function getProcessingExportRequestCount()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		$result = $this->bigDbc->query(
			"SELECT COUNT(`ExportID`) AS `ProcessingRequestCount` ".
			"FROM `Aux`.`LogsExportQ` ".
			"WHERE (`Status`='Spawning' OR `Status`='Processing') ". 
			"AND `ClientLocation`='{$locName}';"
		)->fetch_all(MySQLiResult::AS_ASSOC);
		
		if (empty($result) || empty($result[0]) || empty($result[0]['ProcessingRequestCount']))
			return 0;
		return (int)$result[0]['ProcessingRequestCount'];	
	}
	
	private function cleanupExportStatus()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		// any jobs which have been in 'Spawning' state for more than 30 seconds
		// or in 'Processing' state for more than 8 hours, set status back to 'Pending'
		
		$result = $this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` SET ".
			"`Status`='Pending', ".
			"`Result`='' ".
			"WHERE ((`Status`='Spawning' AND DATE_ADD(`LastServiceAttempt`, INTERVAL {$this->maxSpawningTimeoutSecs} SECOND) < UTC_TIMESTAMP()) ".
			"OR (`Status`='Processing' AND DATE_ADD(`LastServiceAttempt`, INTERVAL ".($this->maxProcessingTimeoutHours*3600)." SECOND) < UTC_TIMESTAMP())) ".
			"AND `ClientLocation`='{$locName}';"
		);
		
		if ($this->bigDbc->affected_rows > 0)
			LogWarning("{$this->context()}Cleaned up {$this->bigDbc->affected_rows} export request(s) that have been stuck in Spawning/Processing state", null, 'U:ARCX');
		else
			LogDebug("{$this->context()}No export request(s) found stuck in Spawning/Processing state");
	}
	
	private function startupCleanupExportStatus()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		// any jobs which have been in 'Spawning' state for more than 30 seconds
		// or in 'Processing' state for more than 8 hours, set status back to 'Pending'
		
		$result = $this->bigDbc->query(
			"UPDATE `Aux`.`LogsExportQ` SET ".
			"`Status`='Pending', ".
			"`Result`='' ".
			"WHERE (`Status`='Spawning' OR `Status`='Processing') ".
			"AND `ClientLocation`='{$locName}';"
		);
		
		if ($this->bigDbc->affected_rows > 0)
			LogWarning("{$this->context()}Cleaned up {$this->bigDbc->affected_rows} export request(s) found in Spawning/Processing state at startup", null, 'U:ARCX');
		else
			LogDebug("{$this->context()}No export request(s) found in Spawning/Processing state at startup");
	}
	
	public function updateExportStatus($exportID, $status, $result=null)
	{
		static $startTime; 
		$additionalFields = '';
		
		switch ($status)
		{
			case 'Spawning':
				$additionalFields = ", `LastServiceAttempt`=UTC_TIMESTAMP(), `NextServiceAttempt`=NULL";
				break;
			
			case 'Processing':
				$startTime = microtime(true/*asFloat*/);
				if ($this->cliMode)
				{
					// no initial spawning (worker process) state
					$additionalFields = ", `LastServiceAttempt`=UTC_TIMESTAMP(), `NextServiceAttempt`=NULL";
				}
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
				
			case 'PurgeComplete':
				// OK
				break;
			
			case 'PurgeFailed':	
				$purgeFailedCount = (int)$result;
				if ($purgeFailedCount < $this->maxPurgeRetries)
				{
					// try again in a while
					$status = 'PendingPurge';
					$retrySeconds = (30 * 60 * pow(2, $purgeFailedCount));
					$additionalFields = ", `NextPurgeAttempt`=DATE_ADD(UTC_TIMESTAMP(), INTERVAL {$retrySeconds} SECOND)";
				}
				$additionalFields .= ", `PurgeFailedCount`=".(++$purgeFailedCount);
				
				break;
				
			default:
				$additionalFields = '';
				break;
		}
		
		$escStatus = $this->bigDbc->escape_string($status);
		
		$sql = "UPDATE `Aux`.`LogsExportQ` ".
			"SET `Status`='{$escStatus}'".
			$additionalFields .
			" WHERE `ExportID`={$exportID};";
			
		LogDebug("{$this->context()}Updating status for export ID {$exportID} to '{$status}'. SQL:\n{$sql}");
		
		$this->bigDbc->query($sql);
		
		return $status;
	}
	
	public function addExportDownload($exportID, $index, $filename)
	{
		$filename = $this->bigDbc->escape_string($filename);
		
		$sql = "INSERT INTO `Aux`.`LogsExportDownloads`(`ExportID`,`Index`,`Filename`) ".
			"VALUES({$exportID}, {$index}, '{$filename}') ".
			"ON DUPLICATE KEY UPDATE `Filename`='{$filename}';";
			
		LogDebug("{$this->context()}Adding export download for export ID {$exportID}, index: {$index}, filename: '{$filename}':\n{$sql}");
		
		$this->bigDbc->query($sql);
	}
	
	private function purge()
	{
		LogDebug("{$this->context()}Checking for exports to purge...");
				
		$requests = $this->getExportRequestsForPurge();	
		if (empty($requests))
		{
			LogVerbose("{$this->context()}No exports to purge");
			return;
		}
		
		LogVerbose("{$this->context()}Found ".count($requests)." export(s) for purge");
		
		$this->loginToCoreApi();
		
		$this->s3endpoints = new ArchivingS3Endpoints($this, $this->context());
				
		foreach ($requests as &$request) 
		{
			try
			{
				$this->purgeRequest($request);
				
				// presumably all download zips in this export have been purged!
				
				$this->updateExportStatus($request['ExportID'], 'PurgeComplete');
			} 
			catch (Exception $ex)
			{
				$status = $this->updateExportStatus($request['ExportID'], 'PurgeFailed', $request['PurgeFailedCount']);
				if ($status == 'PurgeFailed')
				{
					LogWarning("{$this->context()}Failed to purge export: ExportID: {$request['ExportID']}: Exception: {$ex}");
				}
				else // if ($status == 'PendingPurge') // we will retry again in a while
				{
					LogMessage("{$this->context()}Failed to purge export: ExportID: {$request['ExportID']} - retrying again in a while... (Exception: {$ex})");
				}			
			}
		}
	}
	
	private function purgeRequest(&$request)
	{	
		$exportID = $request['ExportID'];
		$orgID =$request['OrgID'];
		$userID = $request['UserID'];
		$type = &$request['Type'];
		$displayType = ($type == 'SMS' ? 'SMS/MMS' : $type);
		$policyID = $request['PolicyID'];
		$downloads = &$request['Downloads'];
					
		LogVerbose("{$this->context()}Purging request: Export ID {$exportID}, Org ID {$exportID}, User ID {$userID}, Type {$displayType}, Downloads Count ".count($downloads));
	
		foreach ($downloads as $index => &$purged) 
		{
			if ($purged)
			{
				LogVerbose("{$this->context()}Download index {$index} already purged skipping");
				continue;
			}
			
			LogVerbose("{$this->context()}Purging download: index {$index} for export ID {$exportID}...");
			
			// Get endpoint details from API
            list($s3endpoint, $bucket, $s3creds) = $this->s3endpoints->get($orgID, $policyID);

			$storageKey = "export/{$orgID}/{$exportID}/{$index}";
			
			LogVerbose("{$this->context()}Checking if S3 object '{$storageKey}' exists in S3 bucket '{$bucket}'");
							
			if (S3::getObjectInfo($bucket, $storageKey, false, $s3endpoint, $s3creds) === true)
			{
				if (!S3::deleteObject($bucket, $storageKey, $s3endpoint, $s3creds))
				{
					throw new Exception("Failed to delete '{$storageKey}' S3 object from S3 bucket '{$bucket}'");
				}

				LogVerbose("{$this->context()}Deleted S3 object '{$storageKey}' from S3 bucket '{$bucket}' OK");
			}
			else
			{
				LogVerbose("{$this->context()}S3 object '{$storageKey}' not found in bucket '{$bucket}', will flag as purged");
			}

			$purged = true;
			
			$this->setExportDownloadAsPurged($exportID, $index);
		}
	}

	private function setExportDownloadAsPurged($exportID, $index)
	{
		$sql = "UPDATE `Aux`.`LogsExportDownloads` SET `Purged`=1 WHERE `ExportID`={$exportID} AND `Index`={$index};";
			
		LogDebug("{$this->context()}Flagging export download index {$index} for export ID {$exportID} as purged");
		
		$this->bigDbc->query($sql);		
	}	

	private function getExportRequestsForPurge()
	{
		$locName = $this->bigDbc->escape_string($this->hAPILocName);
		
		// mark any old complete/failed jobs as 'PendingPurge' so we hide from user before we start purging 
		$sql = 	"UPDATE `Aux`.`LogsExportQ` ".
				"SET `Status`='PendingPurge' ".
				"WHERE `ClientLocation`='{$locName}' ".
				"AND (`Status`='Complete' OR `Status`='Failed') ".
				"AND `Submitted` < SUBDATE(UTC_TIMESTAMP(), INTERVAL {$this->backminutes} MINUTE);";	
					
		LogDebug($sql);
		
		$this->bigDbc->query($sql);
				
		$sql = 	"SELECT ". 
				"q.`ExportID`, ".
				"q.`Submitted`, ".
				"q.`OrgID`, ". 
				"q.`UserID`, ".
				"q.`Type`, ".
				"q.`PolicyID`, ".
				"q.`PurgeFailedCount`, ".
				"d.`Index`, ".
				"d.`Purged` ".
				"FROM ".
				"( ".
					"SELECT ".
					"`ExportID`, ". 
					"`Submitted`, ".
					"`OrgID`, ". 
					"`UserID`, ".
					"`Type`, ".
					"`PolicyID`, ".
					"`PurgeFailedCount` ".
					"FROM `Aux`.`LogsExportQ` ".
					"WHERE `ClientLocation`='{$locName}' ".
					"AND `Status`='PendingPurge' ".
					"AND (UTC_TIMESTAMP() >= `NextPurgeAttempt` OR `NextPurgeAttempt` IS NULL) ".
					"ORDER BY `Submitted` ASC, `ExportID` ASC LIMIT {$this->maxPurgeItems} ".
				") AS q ".
				"LEFT JOIN `Aux`.`LogsExportDownloads` d on q.`ExportID`=d.`ExportID` ". 
				"ORDER BY q.`Submitted` ASC, q.`ExportID` ASC, d.`Index` ASC;";		
					
		LogDebug($sql);
		
		$dbResults = $this->bigDbc->query($sql)->fetch_all(MySQLiResult::AS_ASSOC);		
		if (empty($dbResults))
		{
			// nothing to purge
			return array();
		}
					
		$results = array();
		foreach ($dbResults as $dbResult)
		{		
			$exportID = &$dbResult['ExportID'];
			
			if (!isset($results[$exportID]))
			{
				if (!isset($dbResult['Index'])) 
				{
					// pre LogsExportDownloads table export - spoof a single entry
					$downloads = array(1 => false);
				}
				else
				{
					$index = (int)$dbResult['Index'];
					$purged = (bool)$dbResult['Purged'];
					$downloads = array($index => $purged);
				}
				
				$results[$exportID] = array(
					'ExportID' => (int)$dbResult['ExportID'],
					'Submitted' => $dbResult['Submitted'],
					'OrgID' => (int)$dbResult['OrgID'],
					'UserID' => (int)$dbResult['UserID'],
					'Type' => $dbResult['Type'],
					'PolicyID' => (int)$dbResult['PolicyID'],
					'PurgeFailedCount' => (int)$dbResult['PurgeFailedCount'],
					'Downloads' => $downloads
				);
			}
			else if ($dbResult['Index'])
			{ 
				$index = (int)$dbResult['Index'];
				$purged = (bool)$dbResult['Purged'];
				$results[$exportID]['Downloads'][$index] = $purged;	
			}
		}			
		
		return $results;
	}
	
	public function context()
	{
		if ($this->worker)
		{
			return 'worker: ';
		}
		else if (!$this->cliMode)
		{
			return 'monitor: ';
		}
		else //if ($this->cliMode)
		{
			return 'cli: ';
		}
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
	cli::init(null, array('conf:', 'help', 'no-daemon', 'worker', 'exportid:'));
	if (true === cli::get('help', false))
	{
		Usage();
		exit(0);
	}
	
	$confPath = cli::get('--conf', ArchivingExportApp::CONFIG_PATH);
	$app = ArchivingExportApp::getInstance($confPath);
	$conf = $app->getConf();
	
	// Biggest zip file size we allow is 500Mb by default, but a media file uncompressed could be much bigger than this - we allow up to 3G by default 
	// (and so in copying buffers during decryption we might have up 2X3G sitting in buffers)
	// Note 8G limit is tuned for production cdr boxes so we don't steal more than half the physical memory (16G) 
	$memoryLimit = $conf->get('app.memory_limit', '8G');
	ini_set('memory_limit', $memoryLimit);
	
	if ((false === cli::get('--no-daemon', false)) && (false === cli::get('--worker', false)))
	{
		// daemon monitor
		cleanDirectory(ArchivingExportZip::TEMP_DIR);
		
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
		if ((true === cli::get('--no-daemon', false)) && (false === cli::get('--worker', false)))
		{
			// cli mode
			cleanDirectory(ArchivingExportZip::TEMP_CLI_DIR);
		}
		
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
