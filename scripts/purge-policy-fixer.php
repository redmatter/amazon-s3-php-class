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
require_once "rmdaemonscripts/DaemonScripts.php";

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
        $retention_db = MySQLiTransactional::Get('retention');
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

        $res = $retention_db->query(
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

		$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
		if (!$authHash)
			throw new Exception('No authorization hash specified');

		// Figure out restrictions according to archived-purge configuration
		$PurgeRestrictions = null;
        $archivedConf = $this->_GetArchiveDConf();
		$GetPurgeRestrictions = function() use (&$PurgeRestrictions, $archivedConf)
		{
			if ($PurgeRestrictions !== null)
				return $PurgeRestrictions;

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
		$GetMassUpdateSQL = function($org) use ($GetPurgeRestrictions, $massUpdateSQLCache)
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
				"		{$GetPurgeRestrictions()} ".
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

			$internal_db->query('SET innodb_lock_wait_timeout = 30');
			$retention_db->query('SET innodb_lock_wait_timeout = 30');

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
					$batchCallback($org, $policy, $retentionIds, true);
				}

				// reset the batch ( NOT the delete IDs )
				unset($updatesBatch);
				unset($batchCount);
			}

			// delete items from Queue
            if (count($deleteIDs))
            {
                $sql = sprintf('DELETE FROM `ArchivingInternal`.`ArchiveDPurgeQueue` WHERE `ID` IN ( %s )', implode(', ', $deleteIDs));
                LogDebug($sql);
                $internal_db->query($sql);
            }

			// reset the purge marker
            if (count($orgMinRetentionIds))
            {
                $sql = 'INSERT INTO `ArchivingInternal`.`ArchiveDPurgeMarker` ( `OrgID`, `MaxRetentionID`, `LastResetTime`, `LastRunTime` )' .
                    ' VALUES ';
                foreach ($orgMinRetentionIds as $orgId => $minRetentionId)
                    $sql .= sprintf(' ( %d, %d, "2009-01-01", "2009-01-01" ) ', $orgId, $minRetentionId);
                $sql .= ' ON DUPLICATE KEY UPDATE MaxRetentionID=VALUES(MaxRetentionID), LastRunTime=DATE_ADD(LastRunTime, INTERVAL -60 MINUTE)';
                LogDebug($sql);
                $internal_db->query($sql);
            }

			// fix more than what needs to so that the archived has more chance of clean purge run!
            if (count($orgMaxRetentionIds))
            {
                foreach ($orgMaxRetentionIds as $org => $maxRetentionId)
                {
					$sql = $GetMassUpdateSQL($org);
                    $retention_db->query(sprintf($sql, $maxRetentionId, $maxRetentionId+10000));
                }
            }

			$internal_db->CommitTransaction();
			$retention_db->CommitTransaction();

            unset($orgMinRetentionIds);
            unset($orgMaxRetentionIds);

            $retention_db = MySQLiTransactional::Get('retention', true);

			// fix any that could be in the begining of the table
            $enabledOrgs = $this->_GetEnabledOrgs();
            if ($enabledOrgs === false)
                return 0;

            foreach ($enabledOrgs as $org)
            {
                $retention_db->StartTransaction();

                $sql = $GetMassUpdateSQL($org);

                // fetching the first ID; relying on the table's ordering
                $minId = $retention_db->query(
                    sprintf(
                        'SELECT `ID` FROM `ArchivingRetention`.`Retention_%d` WHERE `State`="Available" %s LIMIT 1',
                        $org, $GetPurgeRestrictions()
                    )
                )->fetch_all();
                $minId = count($minId)? $minId[0]->ID: 1;

                $maxId = $retention_db->query(
                    "SELECT `AUTO_INCREMENT` FROM `INFORMATION_SCHEMA`.`TABLES` ".
                    " WHERE `TABLE_SCHEMA` = 'ArchivingRetention' AND `TABLE_NAME` = 'Retention_{$org}'"
                )->fetch_all();
                $maxId = $maxId[0]->AUTO_INCREMENT;

                for ($idRangeStart = $minId; (($idRangeStart < ($minId + 100000)) && ($idRangeStart < $maxId)); $idRangeStart += 10000)
                {
                    $sqlWithIDsRange = sprintf($sql, $idRangeStart, $idRangeStart+10000);
                    LogDebug($sqlWithIDsRange, '$sqlWithIDsRange');
                    $retention_db->query($sqlWithIDsRange);
                    LogDebug($retention_db->affected_rows, '$retention_db->affected_rows');
                }

                $retention_db->CommitTransaction();
            }
		}
		catch (Exception $e)
		{
			LogError($e);
			$internal_db->RollbackTransaction();
			$retention_db->RollbackTransaction();

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
	cli::init(null, array('auth-hash:', 'conf:', 'help', 'no-daemon'));

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
