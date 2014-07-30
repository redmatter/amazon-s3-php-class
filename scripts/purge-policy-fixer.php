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

require_once 'rmapp/class.BaseApp.php';

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
class PurgePolicyFixerApp extends BaseDaemonApp
{
	const CONFIG_PATH = '/etc/archiving/purge-policy-fixer.conf';

	private $_nextRunTime = 0;

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
	 * run
	 *
	 * @throws Exception
	 *
	 * @return void
	 */
	public function run()
	{
		$now = time();
		if ($this->_nextRunTime > $now)
			return;

		$this->_nextRunTime = $now + 300;

		$config = $this->conf;

		$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
		if (!$authHash)
			throw new Exception('No authorization hash specified');

		// Figure out restrictions according to archived-purge configuration
		$PurgeRestrictions = null;
		$GetPurgeRestrictions = function() use (&$PurgeRestrictions)
		{
			if ($PurgeRestrictions !== null)
				return $PurgeRestrictions;

			$archivedConf = new ConfigMap(Archiving::CONFIG_FILE_RMARCHIVED);
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
			if ($creationDateStart !== false || $creationDateEnd !== false)
			{
				$creationDateRestriction = array();
				if ($creationDateStart !== false)
					$creationDateRestriction[] = "`CreationDate` >= '{$creationDateStart}'";
				if ($creationDateEnd !== false)
					$creationDateRestriction[] = "`CreationDate` <= '{$creationDateEnd}'";

				$restrictions[] = implode(' AND ', $creationDateRestriction);
			}

			return count($restrictions)?
				'/*restrictions start*/ AND ( '.implode(' AND ', $restrictions).' ) /*restrictions end*/':
				'';
		};

		$coreapi = $config->get('api.base_url', 'http://api.redmatter.com');
		API::Init($coreapi, new ArrayStorage());
		API::Login($authHash);
		$api = array($coreapi, API::GetToken());
		/** @noinspection PhpUnusedLocalVariableInspection */
		$authorization_sentry = new ScopeSentry(
			function() use (&$api)
			{
				unset($api);
				API::LogOut();
			}
		);

		/** @var MySQLiTransactional $internal_db */
		$internal_db = MySQLiTransactional::Get('internal');
		/** @var MySQLiTransactional $retention_db */
		$retention_db = MySQLiTransactional::Get('retention');

		$batchCallback = function($org, $policy, $retentionIds, $resetPurgeState) use ($retention_db, $api)
		{
			$retention_db->StartTransaction();

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
			$retention_db->query($sql);

			$retention_db->CommitTransaction();
		};

		try
		{
			$internal_db->StartTransaction();
			$retention_db->StartTransaction();

			$batchCount = 0;
			$updatesBatch = array();
			$deleteIDs = array();
			$orgMinRetentionIds = array();
			$orgMaxRetentionIds = array();

			$internal_db->query(
				"SELECT `ID`, `OrgID`, `RetentionID`, `PolicyID`" .
				" FROM `ArchivingInternal`.`ArchiveDPurgeQueue`" .
				" WHERE `State`=" . Archiving::PurgeStateFromString('init_error') . " AND `HostName`='" . self::_GetHostName() . "'" .
				" ORDER BY `OrgID`, `PolicyID`",
				MYSQLI_USE_RESULT
			)
				->for_each(
					function (MySQLiResult_Row $row) use (&$updatesBatch, &$deleteIDs, &$orgMinRetentionIds, &$orgMaxRetentionIds, &$batchCount, $batchCallback)
					{
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

							// reset the batch ( NOT the delete IDs )
							$updatesBatch = array();
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
					$batchCallback($org, $policy, $retentionIds);
				}

				// reset the batch ( NOT the delete IDs )
				unset($updatesBatch);
				unset($batchCount);
			}

			// delete items from Queue
			$sql = sprintf('DELETE FROM `ArchivingInternal`.`ArchiveDPurgeQueue` WHERE `ID` IN ( %s )', implode(', ', $deleteIDs));
			LogDebug($sql);
			$internal_db->query($sql);

			// reset the purge marker
			$sql = 'INSERT INTO `ArchivingInternal`.`ArchiveDPurgeMarker` ( `OrgID`, `MaxRetentionID`, `LastResetTime`, `LastRunTime` )' .
				' VALUES ';
			foreach ($orgMinRetentionIds as $orgId => $minRetentionId)
				$sql .= sprintf(' ( %d, %d, "2009-01-01", "2009-01-01" ) ', $orgId, $minRetentionId);
			$sql .= ' ON DUPLICATE KEY UPDATE MaxRetentionID=VALUES(MaxRetentionID)';
			LogDebug($sql);
			$internal_db->query($sql);

			// fix more than what needs to so that the archived has more chance of clean purge run!
			foreach ($orgMaxRetentionIds as $org => $maxRetentionId)
			{
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

				$sql = sprintf(
					'UPDATE `ArchivingRetention`.`Retention_%d` ' .
					'SET ' .
					'`RetentionMinDate` = CASE `PolicyID` %s END, ' .
					'`RetentionMaxDate` = CASE `PolicyID` %s END, ' .
					'`PolicyVersion` = CASE `PolicyID` %s END ' .
					'WHERE `ID` > %d AND `State`="Available" %s ' .
					'LIMIT 10000',
					$org,
					$retentionMinUpdate,
					$retentionMaxUpdate,
					$policyVersionUpdate,
					$maxRetentionId,
					$GetPurgeRestrictions()
				);
				LogDebug($sql);
				$retention_db->query($sql);
			}

			unset($orgMinRetentionIds);
			unset($orgMaxRetentionIds);

			$internal_db->CommitTransaction();
			$retention_db->CommitTransaction();
		}
		catch (Exception $e)
		{
			LogError($e);
			$internal_db->RollbackTransaction();
			$retention_db->RollbackTransaction();
		}
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

try
{
	cli::init(null, array('auth-hash:', 'conf:', 'help', 'absolute-stop-date', 'no-daemon'));

	if (true === cli::get('help', false))
	{
		Usage();
		exit(0);
	}

	PurgePolicyFixerApp::getInstance(cli::get('--conf', PurgePolicyFixerApp::CONFIG_PATH))
		->daemonize(cli::get('--no-daemon', false));

	exit(0);
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
