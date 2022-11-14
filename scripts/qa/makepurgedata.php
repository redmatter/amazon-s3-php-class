#!/usr/bin/php54
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

require_once __DIR__ . '/../vendor/autoload.php';

use RedMatter\Services\Helper\SnowPool;
use RedMatter\Services\Helper\SnowTrait;

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
    use SnowTrait;

	const DEFAULT_BATCH_SIZE = 1000;

	const CONF_SAMPLE = '/usr/share/archiving/archiving-makepurgedata.conf.sample';

	private static $_CreationDate;
	private static $_API;

	/**
	 * Get file types
	 *
	 * @param string $types_str config value
	 * @param int    $org       org-id
	 *
	 * @return int[]
	 */
	private static function _GetFileTypes($types_str, $org)
	{
		$ret = array();

		if (0 === strcasecmp($types_str, 'all'))
			return ($org==1)? Archiving::GetAllFileTypes(true) : Archiving::GetAllFileTypes(false);

		foreach (preg_split('/[,;] */', $types_str) as $type)
			if ($type = Archiving::GetFileType($type))
				$ret[] = $type; // anything not UNKNOWN

		return array_unique($ret);
	}

	private static function _CheckHourlyUsageTableExists($org)
    {
        try
        {
            $dbc = MySQLiConnection::Get('retention_ro');
            $res = $dbc->query(
                'SELECT COUNT(*) Count FROM `information_schema`.`TABLES`' .
                " WHERE TABLE_SCHEMA='ArchivingRetention' AND TABLE_NAME='HourlyUsage_{$org}'"
            )->fetch_all();
            return $res[0]->Count > 0;
        }
        catch(Exception $e)
        {
            LogWarning($e);
        }

        return false;
    }

	/**
	 * Verify Org
	 *
	 * @param int $org org
	 *
	 * @return bool
	 */
	private static function _VerifyOrgID($org)
	{
		try
		{
			$dbc = MySQLiConnection::Get('retention_ro');
			$res = $dbc->query(
				"SELECT COUNT(*) `TableCount` FROM `information_schema`.TABLES ".
				"WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND ".
				"  `TABLE_NAME` IN ( 'Usage_{$org}', 'Audit_{$org}', 'Retention_{$org}' )"
			)->fetch_all();
			return $res[0]->TableCount == 3;
		}
		catch(Exception $e)
		{
			LogWarning($e);
		}

		return false;
	}

	/**
	 * Make the ret obj
	 *
	 * @param ArchivingPolicy $policy     policy
	 * @param int             $filetype   file type
	 * @param int             $orgId      org
	 * @param string          $uuid       uuid
	 * @param int             $real_usage real-usage bytes
	 *
	 * @return object
	 */
	private static function _MakeRetentionObj(ArchivingPolicy $policy, $filetype, $orgId, $uuid, $real_usage)
	{
		$now = new DateTime('@'.self::$_CreationDate);
		$creation_date = $now->format('Y-m-d');
		LogDebug($retMin = sprintf('%d %s', $policy->retentionMin[0], $policy->retentionMin[1]), 'RetMIN');
		LogDebug($retMax = sprintf('%d %s', $policy->retentionMax[0], $policy->retentionMax[1]), 'RetMAX');
		$retention_min_date = $now->add(DateInterval::createFromDateString($retMin))->format('Y-m-d');
		$now = new DateTime('@'.self::$_CreationDate);
		$retention_max_date = $now->add(DateInterval::createFromDateString($retMax))->format('Y-m-d');
		$storageEndpoint = ArchivingEndpoint::Get(self::$_API, $orgId, $policy->storageEndpointID);
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
	 * @param array  &$retention_array ret array
	 * @param object $obj              obj
	 *
	 * @return void
	 */
	public static function BatchSaveRetentionObjs(SnowPool $snow, &$retention_array, $obj = null)
	{
		if (!is_null($obj))
		{
			$retention_array[] = $obj;
			if (count($retention_array) < 200)
				return;
		}

		if (!count($retention_array))
			return;

        $hourly_usage_table_exists = self::_CheckHourlyUsageTableExists($retention_array[0]->OrgID);

		$sql_retention = "INSERT INTO `Retention_{$retention_array[0]->OrgID}` (`ID`, `UUID`, `Format`, `Type`, `OrgID`, `PolicyID`, `PolicyVersion`, `CreationDate`, `RetentionMinDate`, `RetentionMaxDate`, `EndpointID`, `UserID` ) VALUES ";
		if ($hourly_usage_table_exists)
        {
            $sql_usage = "INSERT INTO `HourlyUsage_{$retention_array[0]->OrgID}` ( `Hour`, `Type`, `EndpointID`, `Count`, `ClearUsage`, `RealUsage` ) VALUES ";
        }
        else
        {
            $sql_usage = "INSERT INTO `Usage_{$retention_array[0]->OrgID}` ( `Hour`, `Type`, `OrgID`, `EndpointID`, `Count`, `ClearUsage`, `RealUsage` ) VALUES ";
        }
		$first = true;
		foreach ($retention_array as $retention)
		{
            $sql_retention .= sprintf(
                "\n%s( %d, UNHEX(REPLACE('%s', '-', '')), %d, %d, %d, %d, %d, '%s', '%s', '%s', %d, 0 )",
                $first ? '' : ', ',
                $snow->getOneId(),
                $retention->UUID,
                $retention->Format,
                Archiving::GetFileType($retention->Type),
                $retention->OrgID,
                $retention->PolicyID,
                $retention->PolicyVersion,
                $retention->CreationDate,
                $retention->RetentionMinDate,
                $retention->RetentionMaxDate,
                $retention->EndpointID
            );
            if ($hourly_usage_table_exists)
            {
                $sql_usage .= sprintf("\n%s( '%s', %d, %d, %d, %d, %d )", $first? '': ', ', $retention->CreationDate, Archiving::GetFileType($retention->Type), $retention->EndpointID, 1, 0, $retention->RealUsage);
            }
            else
            {
                $sql_usage .= sprintf("\n%s( '%s', '%s', %d, %d, %d, %d, %d )", $first? '': ', ', $retention->CreationDate, $retention->Type, $retention->OrgID, $retention->EndpointID, 1, 0, $retention->RealUsage);
            }
			$first = false;
		}
		// XXX FIXME Duplicates due to successive generation of UUIDs on the same host leading to random pool exhaustion
		$sql_retention .= ' ON DUPLICATE KEY UPDATE `OrgID`=VALUES(`OrgID`)';

		$sql_usage .= ' ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`), `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`), `RealUsage`=`RealUsage`+VALUES(`RealUsage`)';

		LogDebug($sql_retention, 'Retention');
		LogDebug($sql_usage, 'Usage');

		try
		{
			$dbc = MySQLiTransactional::Get('retention_rw');
			$dbc->query($sql_retention);
			$dbc->query($sql_usage);
			$dbc->CommitTransaction();

			static $stdout = null;
			if (null === $stdout && isset($retention_array[0]))
			{
				$stdout = fopen('php://stdout', 'w');
				fputcsv($stdout, array('Type/OrgID/UUID', 'PolicyID', 'PolicyVersion', 'CreationDate', 'RetentionMinDate', 'RetentionMaxDate', 'EndpointID', 'PurgedByDate', 'PurgedByPolicyVersion'));
			}
			foreach ($retention_array as $retention)
				fputcsv($stdout, array("{$retention->Type}/{$retention->OrgID}/{$retention->UUID}", $retention->PolicyID, $retention->PolicyVersion, $retention->CreationDate, $retention->RetentionMinDate, $retention->RetentionMaxDate, $retention->EndpointID, $retention->PurgedByDate, $retention->PurgedByPolicyVersion));

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
	 * @param array  &$headers        headers
	 * @param object $retention       retention
	 * @param bool   $isLegacyMode    is legacy mode
	 * @param bool   $introduceErrors errors anyone?
	 *
	 * @return void
	 */
	private static function _MakeMetaHeaders(&$headers, $retention, $isLegacyMode=false, $introduceErrors=false)
	{
		$headers['file-type'] = 'unknown';
		$headers['file-type-is-compressed'] = 'false';
		$headers['file-type-is-encrypted'] = 'false';

		if ($isLegacyMode)
			return;

		$now = new DateTime('@'.self::$_CreationDate);
		$mangled_date = $now->add(
			DateInterval::createFromDateString(sprintf('%d day', mt_rand(10, 90)))
		)->format('Y-m-d');
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
	 * @throws Exception
	 *
	 * @return void
	 */
	public function run()
	{
		$config = $this->conf;
        $snowPool = $this->getSnowPool();

		$retention_recs = array();
		/** @noinspection PhpUnusedLocalVariableInspection */
		$retention_recs_sentry = new ScopeSentry(
			function() use (&$retention_recs, $snowPool)
			{
				MakePurgeDataApp::BatchSaveRetentionObjs($snowPool, $retention_recs);
			}
		);

		$authCreds = cli::get('--auth-creds', $config->get('api.authorization_creds', false));
		if (!$authCreds)
		{
			$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
			if (!$authHash)
				throw new Exception('No authorization creds or hash specified');
		}

		$org = cli::get('--org', $config->get('dataset.org_id', '1'));
		if (!self::_VerifyOrgID($org))
			throw new Exception('Invalid OrgID specified');

		$filetypes = self::_GetFileTypes(cli::get('--filetype', $config->get('dataset.filetype', 'cdr')), $org);
		if (!count($filetypes))
			throw new Exception('Invalid file types specified');

		self::$_CreationDate = new DateTime(cli::get('--creation-date', $config->get('dataset.creation_date', 'now')));
		self::$_CreationDate = self::$_CreationDate->getTimestamp();

		if ($org != 1 && in_array(Archiving::FILE_TYPE_CDR, $filetypes))
			throw new Exception('CDR not allowed for anyone other than Org:1');

		$batchSize = cli::get('--batch-size', $config->get('dataset.batch_size', self::DEFAULT_BATCH_SIZE));

		$authorization = null;
		$coreapi = $config->get('api.base_url', 'http://cdr.coreapi.redmatter.com');

		if ($authCreds)
		{
			if (strpos($authCreds, ' ') === false) 
				// no scheme specified, assume it's basic auth
				$authCreds = "Basic {$authCreds}";
			$authHdrs = array('Authorization' => $authCreds);
			$authUrl = "{$coreapi}/auth";
		} 
		else 
		{
			// use old auth hash scheme
			$authHdrs = null;
			$authUrl = "{$coreapi}/auth/{$authHash}";
		}

		$stream = HTTPStream::Make('GET', $authUrl, null/*body*/, $authHdrs);
		if ($stream && $stream->httpCode == 201)
		{
			$xpath = new XPath($stream->getContents());
			$authorization = $xpath('/Success/ResourceID');
			LogVerbose('Authenticated');
		}

		if (!$authorization)
			throw new Exception('Failed to authenticate using the credentials given');

		self::$_API = array($coreapi, $authorization);

		/** @noinspection PhpUnusedLocalVariableInspection */
		$authorization_sentry = new ScopeSentry(
			function() use ($authorization, $coreapi)
			{
				HTTPStream::Make('DELETE', "{$coreapi}/auth", null, array('Authorization'=>$authorization));
				LogVerbose('Auth deleted', $authorization);
			}
		);

		$policyVersion = 0;
		$policyID = cli::get('--policy-id', $config->get('dataset.policy_id', 0));
		if (false !== strstr($policyID, ':'))
			list($policyID, $policyVersion) = explode(':', $policyID);

		$policyLatestVersion = null;
		$policy = ArchivingPolicy::Get(array($coreapi, $authorization), $org, $policyID, $policyVersion);
		if ($policyVersion != 0)
			$policyLatestVersion = ArchivingPolicy::Get(array($coreapi, $authorization), $org, $policyID, 0);

		$endpoint = ArchivingEndpoint::Get(array($coreapi, $authorization), $org, $policy->storageEndpointID, true);
		switch ($endpoint->type)
		{
			case 's3':
				$s3creds = new S3Credentials($endpoint->accessKey, $endpoint->secretKey);
				$s3endpoint = new S3EndpointConfig();
				$s3endpoint->withDefaultRegion($endpoint->region);
				break;
			case 's3-emulation':
				if (1 !== preg_match('/^([^\/]+)(.*)$/', $endpoint->baseURI, $matches)) {
					throw new Exception("Malformed BaseURI '{$endpoint->baseURI}' in storage endpoint");
				}
				$s3creds = new S3Credentials($endpoint->accessKey, $endpoint->secretKey);
				$s3endpoint = new S3EndpointConfig($matches[1], $endpoint->region);
				// here, for now we only expect storage-gateway (agw), which does not yet talk HTTPS
				$s3endpoint->withSSLEnabled(false);
				break;
			default:
				throw new Exception("Storage Endpoint Type '{$endpoint->type}' not supported");
				break;
		}

		$isLegacyMode = CLI::get('--legacy-headers', $config->get('dataset.legacy_headers', 'false') != 'false');
		$makeErrors = CLI::get('--introduce-errors', $config->get('dataset.introduce_errors', 'false') != 'false');
		if ($isLegacyMode && $makeErrors)
			LogWarning('The "introduce-errors" mode wouldnt have any effect in "legacy-headers" mode');

		unset($stream);
		unset($xpath);

		// make a set of randomised files
		$files = array();
		call_user_func(
			function() use (&$files)
			{
				$frand = fopen('/dev/urandom', 'r');
				for ($i = 0; $i < 10; $i++)
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
			}
		);

		/** @noinspection PhpUnusedLocalVariableInspection */
		$files_sentry = new ScopeSentry(
			function() use ($files)
			{
				foreach ($files as $f)
				{
					unlink($f[0]);
					LogDebug($f[0], 'Unlinked');
				}
			}
		);


		$filetype = $filetypes[0];
		for ($i = $batchSize; $i > 0; $i--)
		{
			list($file_name, $file_size) = $files[mt_rand(0, 9)];

			if (count($filetypes) != 1)
				$filetype = $filetypes[mt_rand(0, count($filetypes) - 1)];

			$uuid = trim(file_get_contents('/proc/sys/kernel/random/uuid'));

			$storage_key = Archiving::GetFileTypeStr($filetype).'/'.$org.'/'.$uuid;

			$headers = array();
			$retention = self::_MakeRetentionObj($policy, $filetype, $org, $uuid, $file_size);
			self::_MakeMetaHeaders($headers, $retention, $isLegacyMode, $makeErrors);

			LogDebug("New object: {$storage_key} using file: {$file_name}");


			if (!S3::putObjectFile($file_name, $endpoint->bucket, $storage_key, S3::ACL_PRIVATE, $headers,
				'binary/octet-stream', S3::STORAGE_CLASS_STANDARD, S3::SSE_NONE, $s3endpoint, $s3creds))
			{
				throw new Exception("Failed to store {$endpoint->bucket}/{$storage_key}");
			}

			LogDebug($retention, "Retention:$storage_key");
			LogDebug($headers, "Metadata:$storage_key");

			if (null !== $policyLatestVersion)
			{
				$retentionUpToDate = self::_MakeRetentionObj($policyLatestVersion, $filetype, $org, $uuid, $file_size);
				$purgedByDate = $retentionUpToDate->RetentionMaxDate;
				$purgedByPolicyVersion = $retentionUpToDate->PolicyVersion;
			}
			else
			{
				$purgedByDate = $retention->RetentionMaxDate;
				$purgedByPolicyVersion = $retention->PolicyVersion;
			}

			$retention->PurgedByDate = $purgedByDate;
			$retention->PurgedByPolicyVersion = $purgedByPolicyVersion;

			LogMessage("Added {$retention->Type}/{$retention->OrgID}/{$retention->UUID},{$retention->PolicyID},{$retention->PolicyVersion},{$retention->CreationDate},{$retention->RetentionMinDate},{$retention->RetentionMaxDate},{$retention->EndpointID} PurgedBy: {$purgedByDate} ({$purgedByPolicyVersion})");

			// store Retention record
			self::BatchSaveRetentionObjs($snowPool, $retention_recs, $retention);
		}

		return;
	}
}

try
{
	if (true === CLI::get('--help'))
	{
		$prog = basename(__FILE__, '.php');
        // @SkipIndentPreCommitCheckStart
		echo <<<USAGE
Test data generator for QA
$prog [--legacy-headers] --auth-hash=< Hash > --org=< OrgID > --filetype=< FileType1[,FileType2] >
        --policy-id=< PolicyID[:PolicyVersion] > --batch-size=< N >

    --legacy-headers
        Optional; use legacy headers on each file

    --org=< OrgID >
        OrgID for which we are generating test data for

    --filetype=< FileType1[,FileType2] >
        Filetype(s) that need to be created; Operational types are only for Org:1.
        Use --list-operational-filetypes to see a list

    --policy-id=< PolicyID[:PolicyVersion] >
        PolicyID of the Org under which files are to be put

    --batch-size=< N >
        Batch size of the set we are generating; if more than one file types
        are specified, the set will be randomised.

    --auth-creds=< creds >
        API auth creds base64(username:password)

    --auth-hash=< Hash >
        API auth hash SHA1(username).SHA1(password)

    --introduce-errors
        Introduce retention record mismatch ( compared to metadata headers )

    --creation-date=< YYYY-MM-DD >
        Use the specified creation date rather than the current date (NOW)

    --conf=< ConfigPath >
        Path to a config file; sample file available at
        /usr/share/archiving/archiving-makepurgedata.conf.sample

    INFORMATION

    The script will terminate after showing the requested information.

    --help
        This help message

    --list-filetypes
        To see the list of file-types that you can use.

    --list-operational-filetypes
        To see the list of operational file-types that you can use ( only for Org:1 ).


USAGE;
        // @SkipIndentPreCommitCheckEnd
		exit(0);
	}

	$default_conf = getenv('PWD').'/makepurgedata.conf';

	// fudge to get around autoloader error with help or any other informational CLI option
	if (true === CLI::get('--help')
		|| true === CLI::get('--list-filetypes')
		|| true === CLI::get('--list-operational-filetypes'))
	{
		if (!is_readable($default_conf) && is_readable(MakePurgeDataApp::CONF_SAMPLE))
			$default_conf = MakePurgeDataApp::CONF_SAMPLE;
	}

	if (false === CLI::get('--conf', false) && !is_readable($default_conf))
		throw new Exception(
			"No configuration given and no config found at $default_conf; " .
			"use --conf option to specify the config file; ".
			"a sample config file can be found at " . MakePurgeDataApp::CONF_SAMPLE
		);

	$app = MakePurgeDataApp::getInstance(CLI::get('--conf', $default_conf));

	if (true === CLI::get('--list-filetypes'))
	{
		echo "Filetypes available are  ".implode(', ', Archiving::GetAllFileTypesAsString(true))."\n";
		exit(0);
	}
	elseif (true === CLI::get('--list-operational-filetypes'))
	{
		echo "Filetypes available are  ".implode(', ', array_map(array('Archiving', 'GetFileTypeStr'), Archiving::$OperationalDataFileTypesAsString))."\n";
		exit(0);
	}
	else
	{
		$app->run();
		exit(0);
	}
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}
