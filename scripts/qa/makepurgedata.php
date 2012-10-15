#!/usr/bin/php
<?php

/**
 * Make Purge Data (QA)
 *
 * PHP version 5
 *
 * @category QAScripts
 * @package  ArchivingQAScripts
 * @author   Dino Korah <dino.korah@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 *
 */
define('APP_NAME_', 'rm-archiving-mkdata');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_RECURSIVE_LOOP_SAFETY', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);

require_once "s3/S3.php";
require_once 'rmapp/class.BaseApp.php';

/**
* MakePurgeDataApp
*
* @category  MakePurgeDataApp
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class MakePurgeDataApp extends BaseApp
{
	const DEFAULT_BATCH_SIZE = 1000;
	
	private static $creation_date;
	private static $api;
		
	private static function GetFileTypes($types_str, $org)
	{
		$ret = array();

		if (0 === strcasecmp($types_str, 'all'))
			return ($org==1)? array(Archiving::FILE_TYPE_CDR, Archiving::FILE_TYPE_RECORDING, Archiving::FILE_TYPE_SMS):;
					array(Archiving::FILE_TYPE_RECORDING, Archiving::FILE_TYPE_SMS);
		
		foreach (preg_split('/[,;] */', $types_str) as $type)
			if ($type = Archiving::GetFileType($type))
				$ret[] = $type; // anything not UNKNOWN
		
		return array_unique($ret);
	}

	/**
	 * Verify Org
	 * 
	 * @param int $org
	 */
	private static function VerifyOrgID($org)
	{
		try
		{
			$dbc = MySQLiConnection::Get('retention_ro');
			$res = $dbc->query(
					"SELECT COUNT(*) `TableCount` FROM `information_schema`.TABLES ".
					"WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND ".
					"  `TABLE_NAME` IN ( 'Usage_{$org}', 'Audit_{$org}', 'Retention_{$org}' )")->fetch_all();
			return $res[0]->TableCount == 3;
		}
		catch(Exception $e)
		{
			LogWarning($e);
		}
		
		return false;
	}
	
	private static function MakeRetentionObj(ArchivingPolicy $policy, $filetype, $orgId, $uuid, $real_usage)
	{
		$now = new DateTime('@'.self::$creation_date);
		$creation_date = $now->format('Y-m-d');
		LogDebug($retMin = sprintf('%d %s', $policy->retentionMin[0], $policy->retentionMin[1]), 'RetMIN');
		LogDebug($retMax = sprintf('%d %s', $policy->retentionMax[0], $policy->retentionMax[1]), 'RetMAX');
		$retention_min_date = $now->add(DateInterval::createFromDateString($retMin))->format('Y-m-d');
		$now = new DateTime('@'.self::$creation_date);
		$retention_max_date = $now->add(DateInterval::createFromDateString($retMax))->format('Y-m-d');
		$storageEndpoint = ArchivingEndpoint::Get(self::$api, $orgId, $policy->storageEndpointID);
		return (object)array(
				'UUID' => $uuid,
				'OrgID' => $orgId,
				'Type' =>  Archiving::GetFileTypeStr($filetype),
				'Format' => 0 /* unknown */,
				'PolicyID' => $policy->id,
				'PolicyVersion' => $policy->version,
				'EndpointID' => $storageEndpoint->id,
				'UserID' => 0,
				'State' => 'Available',
				'Purged' => 'no',
				'CreationDate' => $creation_date,
				'RetentionMinDate' => $retention_min_date,
				'RetentionMaxDate' => $retention_max_date,
				'RealUsage' => $real_usage
		);
	}
	
	/**
	 * Batch save retention objects
	 * 
	 * @param array  $retention_array
	 * @param object $obj
	 */
	public static function BatchSaveRetentionObjs(&$retention_array, $obj = null)
	{
		if(!is_null($obj))
		{
			$retention_array[] = $obj;
			if(count($retention_array) < 200)
				return;
		}

		if(!count($retention_array))
			return;

		$sql_retention = "INSERT INTO `Retention_{$retention_array[0]->OrgID}` ( `UUID`, `Format`, `Type`, `OrgID`, `PolicyID`, `PolicyVersion`, `CreationDate`, `RetentionMinDate`, `RetentionMaxDate`, `EndpointID`, `UserID` ) VALUES ";
		$sql_usage = "INSERT INTO `Usage_{$retention_array[0]->OrgID}` ( `Hour`, `Type`, `OrgID`, `EndpointID`, `Count`, `ClearUsage`, `RealUsage` ) VALUES ";
		$first = true;
		foreach($retention_array as $retention)
		{
			$sql_retention .= sprintf("\n%s( UNHEX(REPLACE('%s', '-', '')), %d, %d, %d, %d, %d, '%s', '%s', '%s', %d, 0 )", $first? '': ', ', $retention->UUID, $retention->Format, Archiving::GetFileType($retention->Type), $retention->OrgID, $retention->PolicyID, $retention->PolicyVersion, $retention->CreationDate, $retention->RetentionMinDate, $retention->RetentionMaxDate, $retention->EndpointID);
			$sql_usage .= sprintf("\n%s( '%s', '%s', %d, %d, %d, %d, %d )", $first? '': ', ', $retention->CreationDate, $retention->Type, $retention->OrgID, $retention->EndpointID, 1, 0, $retention->RealUsage);
			$first = false;
		}
		$sql_usage .= ' ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`), `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), `RealUsage`=`RealUsage`+VALUES(`RealUsage`)';
		
		LogDebug($sql_retention, 'Retention');
		LogDebug($sql_usage, 'Usage');

		try
		{
			$dbc = MySQLiTransactional::GetConnection('retention_rw');
			$dbc->query($sql_retention);
			$dbc->query($sql_usage);
			$dbc->CommitTransaction();
			
			$retention_array = array();
		}
		catch(Exception $e)
		{
			LogError($e, 'Retetion COMMIT failed');
		}
	}
	
	/**
	 * Make metadata headers from retention record
	 * 
	 * @param array  $headers
	 * @param object $retention
	 * @param bool   $isLegacyMode
	 * @param bool   $introduceErrors
	 */
	private static function MakeMetaHeaders(&$headers, $retention, $isLegacyMode=false, $introduceErrors=false)
	{
		$headers['file-type'] = 'unknown';
		$headers['file-type-is-compressed'] = 'false';
		$headers['file-type-is-encrypted'] = 'false';
		
		if($isLegacyMode)
			return;
		
		$now = new DateTime('@'.self::$creation_date);
		$mangled_date = $now->add(
				DateInterval::createFromDateString(
						sprintf('%d day', mt_rand(10, 90))))->format('Y-m-d');
		$mangle_target = mt_rand(0, 3);
		
		$headers['creation-date'] = ($introduceErrors && ($mangle_target <= 0))? $mangled_date: $retention->CreationDate;
		$headers['policy-id'] = $retention->PolicyID;
		$headers['policy-version'] = $retention->PolicyVersion;
		$headers['real-size'] = $retention->RealUsage;
		$headers['retention-max-date'] = ($introduceErrors && ($mangle_target == 1))? $mangled_date: $retention->RetentionMaxDate;
		$headers['retention-min-date'] = ($introduceErrors && ($mangle_target >= 2))? $mangled_date: $retention->RetentionMinDate;
		$headers['leg-uuid'] = '';

		$headers['qa-created-by'] = 'makepurgedata';
		$headers['qa-introduce-errors'] = $introduceErrors? 'yes': 'no';
		$headers['qa-legacy-mode'] = $isLegacyMode? 'yes': 'no';
	}
	
	/**
	 * run
	 *
	 * @return none
	 */
	public function run()
	{
		$config = $this->conf;
		
		$retention_recs = array();
		$retention_recs_sentry = new ScopeSentry(
				function() use (&$retention_recs)
				{
					MakePurgeDataApp::BatchSaveRetentionObjs($retention_recs);
				});

		$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
		if (!$authHash)
			throw new Exception('No authorization hash specified');

		$org = cli::get('--org', $config->get('dataset.org_id', '1'));
		if (!self::VerifyOrgID($org))
			throw new Exception('Invalid OrgID specified');
		
		$filetypes = self::GetFileTypes(cli::get('--filetype', $config->get('dataset.filetype', 'cdr')), $org);
		if (!count($filetypes))
			throw new Exception('Invalid file types specified');
		
		self::$creation_date = new DateTime(cli::get('--creation-date', $config->get('dataset.creation_date', 'now')));
		self::$creation_date = self::$creation_date->getTimestamp();
		
		if($org != 1 && in_array(Archiving::FILE_TYPE_CDR, $filetypes))
			throw new Exception('CDR not allowed for anyone other than Org:1');

		$batchSize = cli::get('--batch-size', $config->get('dataset.batch_size', self::DEFAULT_BATCH_SIZE));

		$authorization = null;
		$coreapi = $config->get('api.base_url', 'http://api.redmatter.com');
		$stream = HTTPStream::Make('GET', "{$coreapi}/auth/{$authHash}");
		if ($stream && $stream->httpCode == 201)
		{
			$xpath = new XPath($stream->getContents());
			$authorization = $xpath('/Success/ResourceID');
			LogVerbose('Authenticated');
		}

		if (!$authorization)
			throw new Exception('Failed to authenticate using the credentials given');
		
		self::$api = array($coreapi, $authorization);
		
		$authorization_sentry = new ScopeSentry(
				/**
				 * Cleanup function for auth
				 */
				function() use ($authorization, $coreapi)
				{
					$stream = HTTPStream::Make('DELETE', "{$coreapi}/auth", null, array('Authorization'=>$authorization));
					LogVerbose('Auth deleted', $authorization);
				});

		$policyVersion = 0;
		$policyID = cli::get('--policy-id', $config->get('dataset.policy_id', 0));
		if(false !== strstr($policyID, ':'))
			list($policyID, $policyVersion) = explode(':', $policyID);
		
		$policy = ArchivingPolicy::Get(array($coreapi, $authorization), $org, $policyID, $policyVersion);
		$endpoint = ArchivingEndpoint::Get(array($coreapi, $authorization), $org, $policy->storageEndpointID, true);
		if($endpoint->type != 's3')
			throw new Exception('Endpoint is not on Amazon S3');

		$s3 = new S3($endpoint->accessKey, $endpoint->secretKey);

		$isLegacyMode = CLI::get('--legacy-headers', $config->get('dataset.legacy_headers', 'false') != 'false');
		$makeErrors = CLI::get('--introduce-errors', $config->get('dataset.introduce_errors', 'false') != 'false');
		if ($isLegacyMode && $makeErrors)
			LogWarning('The "introduce-errors" mode wouldnt have any effect in "legacy-headers" mode');
		
		unset($stream);
		unset($xpath);
		
		// make a set of randomised files
		$files = array();
		call_user_func(function() use (&$files)
		{
			$frand = fopen('/dev/urandom', 'r');
			for($i = 0; $i < 10; $i++)
			{
				if (!is_readable('/dev/urandom'))
					throw new Exception('/dev/urandom not readable!!');
			
				$file_name = tempnam('/tmp', 'purgedata');
				$file_size = mt_rand(2*1024, 10*1024);
				file_put_contents($file_name, fread($frand, $file_size));
				LogDebug("{$file_name} size: {$file_size}", 'Created');

				$files[] = array($file_name, $file_size);
			}
			fclose($frand);
		});
		
		$files_sentry = new ScopeSentry(function() use ($files)
				{
				foreach($files as $f)
				{
					unlink($f[0]);
					LogDebug($f[0], 'Unlinked');
				}
			});


		$filetype = $filetypes[0];
		for($i = $batchSize; $i > 0; $i--)
		{
			list($file_name, $file_size) = $files[mt_rand(0, 9)];
			
			if(count($filetypes) != 1)
				$filetype = $filetypes[mt_rand(0, count($filetypes) - 1)];
			
			$uuid = UUID::v4();
			
			$storage_key = Archiving::GetFileTypeStr($filetype).'/'.$org.'/'.$uuid;

			$headers = array();
			$retention = self::MakeRetentionObj($policy, $filetype, $org, $uuid, $file_size);
			self::MakeMetaHeaders($headers, $retention, $isLegacyMode, $makeErrors);

			LogMessage("New object: {$storage_key} file: {$file_name}");
			if(!$s3->putObjectFile($file_name, $endpoint->bucket, $storage_key, S3::ACL_PRIVATE, $headers, 'binary/octet-stream'))
				throw new Exception("Failed to store {$endpoint->bucket}/{$storage_key}");

			LogDebug($retention, "Retention:$storage_key");
			LogDebug($headers, "Metadata:$storage_key");

			// store Retention record
			self::BatchSaveRetentionObjs($retention_recs, $retention);
		}

		return;
	}
}

try
{
	if (true === CLI::get('--help'))
	{
		$prog = basename(__FILE__, '.php');
		echo <<<USAGE
Test data generator for QA
$prog [--legacy-headers] --auth-hash=< Hash > --org=< OrgID > --filetype=< FileType1[,FileType2] >
        --policy-id=< PolicyID[:PolicyVersion] > --batch-size=< N >

    --legacy-headers
        Optional; use legacy headers on each file

    --org=< OrgID >
        OrgID for which we are generating test data for

    --filetype=< FileType1[,FileType2] >
        Filetype(s) that need to be created; cdr only for Org:1
        FileType = cdr | recording | sms

    --policy-id=< PolicyID[:PolicyVersion] >
        PolicyID of the Org under which files are to be put

    --batch-size=< N >
	    Batch size of the set we are generating; if more than one file types
	    are specified, the set will be randomised.

    --auth-hash=< Hash >
        API auth hash SHA1(username).SHA1(password)

    --introduce-errors
        Introduce retention record mismatch ( compared to metadata headers )
        
    --creation-date=< YYYY-MM-DD >
        Use the specified creation date rather than the current date (NOW)

    --conf=< ConfigPath >
        Path to a config file; sample file available at
        /usr/share/archiving/archiving-makepurgedata.conf.sample

    --help
	    This help message


USAGE;
		exit(0);
	}
	
	$default_conf = getenv('PWD').'/makepurgedata.conf';
	
	if (false === CLI::get('--conf', false) && !is_readable($default_conf))
		throw new Exception("No configuration given and no config found at $default_conf; use --conf option to specify the config file");

	MakePurgeDataApp::getInstance( CLI::get('--conf', $default_conf) )->run();
	exit(0);
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}
