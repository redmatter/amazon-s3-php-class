#!/usr/bin/php
<?php

/**
 * PurgePolicyFixerApp class
 *
 * PHP version 5
 *
 * @category Archiving
 * @package  RedMatter_Archiving
 * @author   Dino Korah <dino.korah@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 *
 */
define('APP_NAME_', 'rm-archpolfix');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

require_once 's3/S3.php';
require_once 'rmapp/class.BaseApp.php';
require_once 'rmdaemonscripts/DaemonScripts.php';

/**
 * PurgePolicyFixerApp
 *
 * @category  Archiving
 * @package   RedMatter_Archiving
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class PurgePolicyFixerApp extends BaseApp
{
	const CONFIG_PATH = '/etc/archiving/purge-policy-fixer.conf';
	const TIMEOUT_MASS_UPDATE_SQL_CACHE = 300 /* seconds */;
	const TIMEOUT_ARCHIVED_CONF = 300 /* seconds */;

	private $_nextRunTime = 0;

	protected $massUpdateSQLCache = array();

	private $_archivedConf = null;
	private $_archivedConfTimeout = null;

	/**
	 * Get the config obj for archived conf
	 *
	 * @return ConfigMap
	 */
	private function _GetArchiveDConf()
	{
		$now = time();

		if ($this->_archivedConf !== null)
		{
			if ($this->_archivedConfTimeout > $now)
				return $this->_archivedConf;
		}

		$this->_archivedConf = new ConfigMap(Archiving::CONFIG_FILE_RMARCHIVED);
		$this->_archivedConfTimeout = $now + self::TIMEOUT_ARCHIVED_CONF;

		return $this->_archivedConf;
	}

	/**
	 * Get hostname (not FQDN) and cache it
	 *
	 * @return string
	 */
	private static function _GetHostName()
	{
		static $hostname = null;
		if (null === $hostname)
		{
			$hostname = gethostname();
			if (false !== $hostname)
			{
				$firstDot = strpos($hostname, '.');
				if (false !== $firstDot)
					$hostname = substr($hostname, 0, $firstDot);
			}
			else
				$hostname = 'unknown-error';
		}

		return $hostname;
	}

	/**
	 * Get all enabled OrgIDs
	 *
	 * @return int[]|bool false for none, or an array of enabled OrgIDs
	 */
	private function _GetEnabledOrgs()
	{
		$retentionDb = MySQLiTransactional::Get('retention');
		$archivedConfig = $this->_GetArchiveDConf();
		$enabledOrgs = $archivedConfig->get('retention_purge.enabled_orgs', '');
		$enabledOrgs = strtolower($enabledOrgs);

		if ($enabledOrgs === '' || $enabledOrgs === 'none')
			return false;

		if ($enabledOrgs !== 'all')
		{
			$enabledOrgs = preg_split('/[,;] */', $enabledOrgs);
			$enabledOrgs = array_map(
				function($val)
				{
					return (int)$val;
				},
				$enabledOrgs
			);

			$enabledOrgs = array_filter(
				$enabledOrgs,
				function($val)
				{
					return $val > 0;
				}
			);

			if (!count($enabledOrgs))
				return false;

			if (1 == count($enabledOrgs))
				$where = "X.OrgID = {$enabledOrgs[0]}";
			else
				$where = sprintf('X.OrgID IN (%s)', implode(',', $enabledOrgs));
		}
		else
		{
			$where = '1';
		}

		$res = $retentionDb->query(
			"SELECT OrgID ".
			"  FROM ( ".
			"    SELECT SUBSTRING(TABLE_NAME FROM 7) `OrgID` ".
			"    FROM `information_schema`.TABLES ".
			"    WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND TABLE_NAME LIKE 'Usage_%' AND TABLE_NAME != 'Usage_Template' ".
			"  ) X ".
			"WHERE ( $where ) AND ( X.OrgID % 47 ) % @@auto_increment_increment + 1 = @@auto_increment_offset"
		)->fetch_all();

		if (!count($res))
			return false;

		return array_map(
			function (MySQLiResult_Row $row)
			{
				return (int)($row->OrgID);
			},
			$res
		);
	}

	/**
	 * run
	 *
	 * @throws Exception
	 *
	 * @return integer
	 */
	public function run()
	{
		$now = time();
		if ($this->_nextRunTime > $now)
			return 0;

		$this->_nextRunTime = $now + 300;

		$config = $this->conf;

		$authCreds = cli::get('--auth-creds', $config->get('api.authorization_creds', false));
		if (!$authCreds)
		{
			$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
			if (!$authHash)
				throw new Exception('No authorization creds or hash specified');
		}

		// Figure out restrictions according to archived-purge configuration
		$purgeRestrictions = null;
		$archivedConf = $this->_GetArchiveDConf();
		$getPurgeRestrictions = function() use (&$purgeRestrictions, $archivedConf)
		{
			if ($purgeRestrictions !== null)
				return $purgeRestrictions;

			$restrictions = array();

			$enabledFileTypes = $archivedConf->get('retention_purge.enabled_filetypes', 'cdr');
			if ($enabledFileTypes != 'all')
			{
				$fileTypesRestriction = array();
				foreach (preg_split('/[,;] */', $enabledFileTypes) as $type)
				{
					$type = Archiving::GetFileType($type);
					if ($type != Archiving::FILE_TYPE_UNKNOWN)
						$fileTypesRestriction[] = $type;
				}

				$fileTypesRestriction = array_unique($fileTypesRestriction);
				if (isset($fileTypesRestriction[1]))
					$restrictions[] = sprintf('`Type` IN ( %s )', implode(', ', $fileTypesRestriction));
				else
					$restrictions[] = sprintf('`Type`=%d', $fileTypesRestriction[0]);
			}

			$creationDateStart = $archivedConf->get('retention_purge.creation_date_range_start', false);
			$creationDateEnd = $archivedConf->get('retention_purge.creation_date_range_end', false);
			if ($creationDateStart || $creationDateEnd)
			{
				$creationDateRestriction = array();
				if ($creationDateStart)
					$creationDateRestriction[] = "`CreationDate` >= '{$creationDateStart}'";

				if ($creationDateEnd)
					$creationDateRestriction[] = "`CreationDate` <= '{$creationDateEnd}'";

				$restrictions[] = implode(' AND ', $creationDateRestriction);
			}

			return count($restrictions)?
				'/*restrictions start*/ AND ( '.implode(' AND ', $restrictions).' ) /*restrictions end*/':
				'';
		};

		$massUpdateSQLCache =& $this->massUpdateSQLCache;
		$getMassUpdateSQL = function($org) use ($getPurgeRestrictions, $massUpdateSQLCache)
		{
			$now = time();
			if (isset($massUpdateSQLCache[$org]) && $massUpdateSQLCache[$org]['expireTime'] >= $now)
				return $massUpdateSQLCache[$org]['sql'];

			$policyVersionUpdate = '';
			$retentionMinUpdate = '';
			$retentionMaxUpdate = '';

			// get all policies and their latest version
			$xpath = API::Get("/archiving/policy/{$org}/all");
			foreach ($xpath('/Archiving/Policies/Policy', true) as $policyNode)
			{
				$policyId = $xpath('ID', false, $policyNode);
				$policyVersion = $xpath('Version', false, $policyNode);
				$policyVersionUpdate .= sprintf(' WHEN %d THEN %d', $policyId, $policyVersion);
				$retentionMinUpdate .= sprintf(
					' WHEN %d THEN DATE_ADD( `CreationDate`, %s )',
					$policyId,
					ArchivingPolicy::GetSQLIntervalSpec(
						array(
							$xpath('RetentionMin', false, $policyNode),
							$xpath('RetentionMin/@Unit', false, $policyNode)
						)
					)
				);
				$retentionMaxUpdate .= sprintf(
					' WHEN %d THEN DATE_ADD( `CreationDate`, %s )',
					$policyId,
					ArchivingPolicy::GetSQLIntervalSpec(
						array(
							$xpath('RetentionMax', false, $policyNode),
							$xpath('RetentionMax/@Unit', false, $policyNode)
						)
					)
				);
			}

			$sql = "UPDATE `ArchivingRetention`.`Retention_{$org}` orig ".
				"INNER JOIN ".
				"( ".
				"	SELECT `ID`, `PolicyID`, ".
				"		CASE `PolicyID` {$retentionMinUpdate}  END AS RetentionMinDate_NEW, `RetentionMinDate`, ".
				"		CASE `PolicyID` {$retentionMaxUpdate}  END AS RetentionMaxDate_NEW, `RetentionMaxDate`, ".
				"		CASE `PolicyID` {$policyVersionUpdate} END AS PolicyVersion_NEW, `PolicyVersion` ".
				"	FROM `ArchivingRetention`.`Retention_{$org}` ".
				"	WHERE `ID` > %d AND `ID` < %d AND `State`='Available' ".
				"		{$getPurgeRestrictions()} ".
				"	HAVING PolicyVersion != PolicyVersion_NEW ".
				") AS upd ".
				"ON orig.`ID`=upd.`ID` ".
				"SET orig.`RetentionMinDate` = upd.`RetentionMinDate_NEW`, ".
				"	orig.`RetentionMaxDate` = upd.`RetentionMaxDate_NEW`, ".
				"	orig.`PolicyVersion`    = upd.`PolicyVersion_NEW` ";

			$massUpdateSQLCache[$org] = array(
				'expireTime'=>$now+PurgePolicyFixerApp::TIMEOUT_MASS_UPDATE_SQL_CACHE,
				'sql'=>$sql
			);

			LogDebug($sql);
			return $sql;
		};

		$coreapi = $config->get('api.base_url', 'http://api.redmatter.com');
		API::Init($coreapi, new ArrayStorage(), APP_NAME_.':1.0');
		if ($authCreds)
			API::LoginBasicAuth($authCreds, null/*mfa*/, true/*re_authenticate*/);
		else
			// use old auth hash scheme
			API::Login($authHash);

		$api = array($coreapi, API::GetToken());
		/** @noinspection PhpUnusedLocalVariableInspection */
		$authorizationSentry = new ScopeSentry(
			function() use (&$api)
			{
				unset($api);
				API::LogOut();
			}
		);

		/** @var MySQLiTransactional $internalDb */
		$internalDb = MySQLiTransactional::Get('internal');
		/** @var MySQLiTransactional $retentionDb */
		$retentionDb = MySQLiTransactional::Get('retention');

		$batchCallback = function($org, $policy, $retentionIds, $resetPurgeState) use ($retentionDb, $api)
		{
			$retentionDb->StartTransaction();

			$policy = ArchivingPolicy::Get($api, $org, $policy, 0);

			$sql = sprintf(
				'UPDATE `ArchivingRetention`.`Retention_%d` SET'.
				'`RetentionMinDate` = DATE_ADD( `CreationDate`, %s ), '.
				'`RetentionMaxDate` = DATE_ADD( `CreationDate`, %s ), '.
				'`PolicyVersion` = %d'.
				'%s WHERE `ID` IN ( %s )' /* $resetPurgeState */,
				$org,
				ArchivingPolicy::GetSQLIntervalSpec($policy->retentionMin),
				ArchivingPolicy::GetSQLIntervalSpec($policy->retentionMax),
				$policy->version,
				($resetPurgeState? ", `State`='Available'": ''),
				implode(', ', $retentionIds)
			);
			LogDebug($sql);
			$retentionDb->query($sql);

			$retentionDb->CommitTransaction();
		};

		$fixS3HeaderAbsoluteStopDate = $config->get('s3_header_fix.absolute_stop_date');
		$fixS3Headers = function($orgId, array $retentionIds) use ($retentionDb, $fixS3HeaderAbsoluteStopDate, $api)
		{
			if (!$fixS3HeaderAbsoluteStopDate/* === null */ || !count($retentionIds))
				return;

			$retentionDb->query(
				"SELECT" .
				" ID," .
				" IF(Format & (0x00f0 << 8), 'true', 'false') Encrypted," .
				" IF(Format & (0x000f << 8), 'true', 'false') Compressed," .
				" Format & 0xff SubType," .
				" Type, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, EndpointID, " .
				Archiving::SqlSelectUuid('UUID', 'UUID').", ".Archiving::SqlSelectUuid('LegUUID', 'LegUUID')."," .
				" ISNULL(retention.LegUUID) `LegUUIDNull`" .
				"FROM ArchivingRetention.Retention_{$orgId} retention " .
				"WHERE ID IN (".implode(', ', $retentionIds).")" .
				" AND retention.CreationDate < '{$fixS3HeaderAbsoluteStopDate}'"
			)
				->for_each(
					// Record handler lambda that gets records from retention database and compares them with S3 headers
					function(MySQLiResult_Row $row) use ($orgId, $api)
					{
						$identifier = Archiving::ConstructIdentifier($row->Type, $orgId, $row->UUID);
						$endpoint = ArchivingEndpoint::Get($api, $orgId, $row->EndpointID, true);

						if ($endpoint->type != 's3')
						{
							LogMessage('Ignoring object from non-S3 endpoint', $identifier);
							return;
						}

						$s3 = new S3($endpoint->accessKey, $endpoint->secretKey);

						$inf = $s3->getObjectInfo($endpoint->bucket, $identifier);
						LogDebug(var_export($inf, true), 'Metadata from S3 for '.$identifier);
						$realSize = $inf['size'];

						$headers = array(
							'creation-date' => $row->CreationDate,
							'file-type' => Archiving::GetSubFormatStr($row->SubType),
							'file-type-is-compressed' => $row->Compressed,
							'file-type-is-encrypted' => $row->Encrypted,
							'policy-id' => $row->PolicyID,
							'policy-version' => $row->PolicyVersion,
							'real-size' => $realSize,
							'retention-max-date' => $row->RetentionMaxDate,
							'retention-min-date' => $row->RetentionMinDate,
							'leg-uuid' => ($row->LegUUIDNull == 1)? '': $row->LegUUID
						);

						$headerMismatch = false;
						foreach ($headers as $hKey => $hValue)
						{
							$amzKey = sprintf('x-amz-meta-%s', $hKey);
							$amzValue = isset($inf[$amzKey])? $inf[$amzKey]: null;
							if (is_null($amzValue) || ($amzValue != $hValue))
							{
								$headerMismatch = true;
								LogMessage("Metadata mismatch: {$hKey}: (s3) '{$amzValue}' != (db) '{$hValue}'", $identifier);
							}
						}

						if ($headerMismatch)
						{
							$retry = 5;
							$copyOk = null;
							do
							{
								// S3 copy on to itself to update headers
								$copyOk = $s3->copyObject(
									$endpoint->bucket, $identifier, $endpoint->bucket, $identifier, S3::ACL_PRIVATE,
									$headers, array('Content-Type'=>'binary/octet-stream')
								);
								if (false === $copyOk)
								{
									if ($retry)
									{
										$delay = pow(6 - $retry, 2);
										LogWarning("Error copying new metadata; retrying in $delay seconds (sleeping..)", $identifier);

										// TODO: Make use of daemonscripts sleep function.
										sleep($delay);
									}
									else
										LogError('Error copying new metadata; giving up!', $identifier);
								}
							}
							while ($retry-- && !$copyOk);

							LogMessage(sprintf('outcome:%s', $copyOk? 'ok': 'failed'), $identifier);
						}
						else
							LogMessage('No mismatch in metadata', $identifier);

						unset($s3);
					}
				);
		};

		try
		{
			$internalDb->StartTransaction();
			$retentionDb->StartTransaction();

			$internalDb->query('SET innodb_lock_wait_timeout = 30');
			$retentionDb->query('SET innodb_lock_wait_timeout = 30');

			$batchCount = 0;
			$updatesBatch = array();
			$deleteIDs = array();
			$orgMinRetentionIds = array();
			$orgMaxRetentionIds = array();
			$orgHeaderFixRetentionIds = array();

			$internalDb->query(
				"SELECT `ID`, `OrgID`, `RetentionID`, `PolicyID`," .
				" `CreationDate` <= '$fixS3HeaderAbsoluteStopDate' `PossiblyNeedHeaderFix`" .
				" FROM `ArchivingInternal`.`ArchiveDPurgeQueue`" .
				" WHERE `State`=" . Archiving::PurgeStateFromString('init_error') .
				"  AND `HostName`='" . self::_GetHostName() . "'" .
				" ORDER BY `OrgID`, `PolicyID`",
				MYSQLI_USE_RESULT
			)
				->for_each(
					function (MySQLiResult_Row $row) use (
						&$updatesBatch, &$deleteIDs, &$orgMinRetentionIds, &$orgMaxRetentionIds,
						&$batchCount, $batchCallback, $fixS3Headers, &$orgHeaderFixRetentionIds)
					{
						/** @var MySQLiResult_Row|stdClass $row */
						$orgId = (int)($row->OrgID);
						$retentionId = (int)($row->RetentionID);
						$batchKey = "{$orgId}:{$row->PolicyID}";
						if (!isset($updatesBatch[$batchKey]))
							$updatesBatch[$batchKey] = array();

						// build the batch
						$updatesBatch[$batchKey][] = $retentionId;

						// collect queue-ID into $deleteIDs
						$deleteIDs[] = (int)($row->ID);
						if (!isset($orgMinRetentionIds[$orgId]) || $retentionId < $orgMinRetentionIds[$orgId])
							$orgMinRetentionIds[$orgId] = $retentionId;

						if (!isset($orgMaxRetentionIds[$orgId]) || $retentionId > $orgMaxRetentionIds[$orgId])
							$orgMaxRetentionIds[$orgId] = $retentionId;

						// track the items which possibly need header fixing
						if ($row->PossiblyNeedHeaderFix)
						{
							if (!isset($orgHeaderFixRetentionIds[$orgId]))
								$orgHeaderFixRetentionIds[$orgId] = array($retentionId);
							else
								$orgHeaderFixRetentionIds[$orgId][] = $retentionId;
						}

						$batchCount++;

						if ($batchCount > 1000)
						{
							foreach ($updatesBatch as $orgPolicy => $retentionIds)
							{
								list($org, $policy) = explode(':', $orgPolicy);
								$batchCallback($org, $policy, $retentionIds, true);

								// get rid of the batch
								$updatesBatch[$orgPolicy] = null;
							}

							foreach ($orgHeaderFixRetentionIds as $orgId => $retentionIds)
							{
								$fixS3Headers($orgId, $retentionIds);
							}

							// reset the batch ( NOT the delete IDs )
							$updatesBatch = array();
							$orgHeaderFixRetentionIds = array();
							$batchCount = 0;
						}
					}
				);

			// sort out any remaining in the last / partial batch
			if ($batchCount)
			{
				foreach ($updatesBatch as $orgPolicy => $retentionIds)
				{
					list($org, $policy) = explode(':', $orgPolicy);
					$batchCallback($org, $policy, $retentionIds, true);
				}

				foreach ($orgHeaderFixRetentionIds as $orgId => $retentionIds)
				{
					$fixS3Headers($orgId, $retentionIds);
				}

				// reset the batch ( NOT the delete IDs )
				unset($updatesBatch);
				unset($orgHeaderFixRetentionIds);
				unset($batchCount);
			}

			// delete items from Queue
			if (count($deleteIDs))
			{
				$sql = sprintf('DELETE FROM `ArchivingInternal`.`ArchiveDPurgeQueue` WHERE `ID` IN ( %s )', implode(', ', $deleteIDs));
				LogDebug($sql);
				$internalDb->query($sql);
			}

			// reset the purge marker
			if (count($orgMinRetentionIds))
			{
				$first = true;
				$sql = 'INSERT INTO `ArchivingInternal`.`ArchiveDPurgeMarker` ( `OrgID`, `MaxRetentionID`, `LastResetTime`, `LastRunTime` )' .
					' VALUES ';
				foreach ($orgMinRetentionIds as $orgId => $minRetentionId)
				{
					if (!$first)
						$sql .= ', ';
					$sql .= sprintf('( %d, %d, "2009-01-01", "2009-01-01" ) ', $orgId, $minRetentionId);
					$first = false;
				}
				$sql .= ' ON DUPLICATE KEY UPDATE MaxRetentionID=VALUES(MaxRetentionID), LastRunTime=DATE_ADD(LastRunTime, INTERVAL -60 MINUTE)';
				LogDebug($sql);
				$internalDb->query($sql);
			}

			// fix more than what needs to so that the archived has more chance of clean purge run!
			if (count($orgMaxRetentionIds))
			{
				foreach ($orgMaxRetentionIds as $org => $maxRetentionId)
				{
					$sql = $getMassUpdateSQL($org);
					$retentionDb->query(sprintf($sql, $maxRetentionId, $maxRetentionId+10000));
				}
			}

			$internalDb->CommitTransaction();
			$retentionDb->CommitTransaction();

			unset($orgMinRetentionIds);
			unset($orgMaxRetentionIds);

			$retentionDb = MySQLiTransactional::Get('retention', true);

			// fix any that could be in the begining of the table
			$enabledOrgs = $this->_GetEnabledOrgs();
			if ($enabledOrgs === false)
				return 0;

			foreach ($enabledOrgs as $org)
			{
				$retentionDb->StartTransaction();

				$sql = $getMassUpdateSQL($org);

				// fetching the first ID; relying on the table's ordering
				$minId = $retentionDb->query(
					sprintf(
						'SELECT `ID` FROM `ArchivingRetention`.`Retention_%d` WHERE `State`="Available" %s LIMIT 1',
						$org, $getPurgeRestrictions()
					)
				)->fetch_all();
				$minId = count($minId)? $minId[0]->ID: 1;

				$maxId = $retentionDb->query(
					"SELECT `AUTO_INCREMENT` FROM `INFORMATION_SCHEMA`.`TABLES` ".
					" WHERE `TABLE_SCHEMA` = 'ArchivingRetention' AND `TABLE_NAME` = 'Retention_{$org}'"
				)->fetch_all();
				$maxId = $maxId[0]->AUTO_INCREMENT;

				for ($idRangeStart = $minId; (($idRangeStart < ($minId + 100000)) && ($idRangeStart < $maxId)); $idRangeStart += 10000)
				{
					$sqlWithIDsRange = sprintf($sql, $idRangeStart, $idRangeStart+10000);
					LogDebug($sqlWithIDsRange, '$sqlWithIDsRange');
					$retentionDb->query($sqlWithIDsRange);
					LogDebug($retentionDb->affected_rows, '$retention_db->affected_rows');
				}

				$retentionDb->CommitTransaction();
			}
		}
		catch (Exception $e)
		{
			LogError($e);
			$internalDb->RollbackTransaction();
			$retentionDb->RollbackTransaction();

			return 1;
		}

		return 0;
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

Archiving Policy Fixer (for Retention Records)$error
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

	$app = 	PurgePolicyFixerApp::getInstance(cli::get('--conf', PurgePolicyFixerApp::CONFIG_PATH));
	$conf = $app->getConf();

	if (false === cli::get('--no-daemon', false))
	{
		DaemonScripts::Initialise(
			array(
				'runasUid' => $conf->get('app.run_as_user', 'cdrmunch'),
				'runasGid' => $conf->get('app.run_as_group', 'cdrmunch'),
				'pidfilePath' => $conf->get('app.pid_file', '/var/run/archiving/rmarchpolfix.pid'),
				'signalHandlers' => array(SIGTERM => $terminateCallback, SIGINT => $terminateCallback),
				'sleepInterval' => array(5 /*seconds and*/, 0 /* micro second */),
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
catch(CLIUsageErrorException $e)
{
	Usage($e->getMessage());
	exit(1);
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}
