#!/usr/bin/php
<?php
//NoSniff

/**
 * Migration Verify
 *
 * PHP version 5
 *
 * @category  Files
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
define('CONF_FILE', 'verify-migration.conf');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);

require_once 'rmapp/class.BaseApp.php';
define('APP_NAME_', 'rm-verify-migration');

require_once 's3/S3.php';

/**
 * Verify Migration
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */

class VerifyMigrationApp extends BaseApp
{
	public function showUsage($exit_status=1, $message=null)
	{
		if (isset($message))
			echo "$message\n\n";
		echo "Usage:\n";
		echo "verify-migration.php --s3-access-key=<S3AccessKey> --s3-secret-key=<S3SecretKey> [--s3-bucket=<S3Bucket>] --conf=<ConfFile default:verify-migration.conf> [--check-orgs=<OrgList>] [--log-level=<LogLevel>] [--host-index=<HostIndex>]\n";
		
		exit($exit_status);
	}
	
	/**
	 * run()
	 *
	 * @return integer
	 */
	public function run()
	{
		$migDb = $this->getDb('migration');
		$retDb = $this->getDb('retention');
		
		$conf = $this->getConf();
		
		$hostCount = $conf->get('host_count', 4);;
		$hostIndex = cli::get('--host-index', $conf->get('host_index'));
		if (!isset($hostIndex))
			$this->showUsage(1, "HostIndex NOT given");
		
		$s3AccessKey = cli::get('--s3-access-key');
		$s3SecretKey = cli::get('--s3-secret-key');
		$s3Bucket = cli::get('--s3-bucket', $conf->get('s3.bucket', 'rm-archiving'));
		if (!isset($s3AccessKey, $s3SecretKey))
			$this->showUsage(1, "S3 Credentials NOT given");
			
		$checkOrgs = cli::get('--check-orgs');
			
		$s3creds = new S3Credentials($s3AccessKey, $s3SecretKey);
		$s3endpoint = new S3EndpointConfig();
		
		$lastID = cli::get('--start-id', 0);

		$count_redoIDs = 0;
		$count_setDoneIDs = 0;
		
		while ($res = $migDb->Execute("SELECT ID, LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID`, HEX(UUID) `UUID_HEX`, OrgID, Status FROM Migration WHERE Status IN ('migrated', 'pending') AND ID > {$lastID} ".(isset($checkOrgs)? " AND OrgID IN ({$checkOrgs})": '')." AND (( ID % {$hostCount} ) = {$hostIndex}) LIMIT 100"))
		{
			if (!$res->RecordCount())
				break;
				
			$redoIDs = array();
			$redoOrgUUIDs = array();
			$setDoneIDs = array();
			
			while ($row = $res->FetchRow())
			{
				$s3Key = "recording/{$row[3]}/{$row[1]}";
				
				$savedToS3 = ( false !== S3::getObjectInfo($s3Bucket, $s3Key, false, $s3endpoint, $s3creds) );

				$retRes = $retDb->Execute("SELECT ID FROM Retention_{$row[3]} WHERE UUID=0x{$row[2]}");
				$retentionSaved = ( 0 != $retRes->RecordCount() );
				$retRes->Close();
				
				LogVerbose($savedToS3, "Org:{$row[3]} UUID:{$row[1]} - SavedToS3");
				LogVerbose($retentionSaved, "Org:{$row[3]} UUID:{$row[1]} - RetentionSaved");
				
				if ($savedToS3 && $retentionSaved && ($row[4] === 'migrated'))
				{
					LogVerbose('OK', "Org:{$row[3]} UUID:{$row[1]}");
				}
				elseif ($savedToS3 && $retentionSaved && ($row[4] !== 'migrated'))
				{
					LogVerbose('SetDone', "Org:{$row[3]} UUID:{$row[1]}");
					$setDoneIDs[] = $row[0];
				}
				else
				{
					LogVerbose('ReDo', "Org:{$row[3]} UUID:{$row[1]}");
					if(!isset($redoOrgUUIDs[$row[3]]))
						$redoOrgUUIDs[$row[3]] = array();
						
					$redoOrgUUIDs[$row[3]][] = $row[2]; // UUID_HEX
					
					$redoIDs[] = $row[0]; // ID
				}
				
				$lastID = $row[0];
			}
			
			if (isset($setDoneIDs[0]))
			{
				if ('no' === cli::get('--readonly', 'yes'))
					$migDb->Execute("UPDATE Migration SET Status='migrated' WHERE ID IN (".implode(',', $setDoneIDs).")");
				else
					LogMessage("UPDATE Migration SET Status='migrated' WHERE ID IN (".implode(',', $setDoneIDs).")");

				$count_setDoneIDs += count($setDoneIDs);
			}

			if (isset($redoIDs[0]))
			{
				if ('no' === cli::get('--readonly', 'yes'))
					$migDb->Execute("UPDATE Migration SET Status='pending' WHERE ID IN (".implode(',', $redoIDs).")");
				else
					LogMessage("UPDATE Migration SET Status='pending' WHERE ID IN (".implode(',', $redoIDs).")");
					
				foreach ($redoOrgUUIDs as $org => $uuids)
				{
					if ('no' === cli::get('--readonly', 'yes'))
						$retDb->Execute("DELETE FROM Retention_{$org} WHERE UUID IN (0x".implode(', 0x', $uuids).")");
					else
						LogMessage("DELETE FROM Retention_{$org} WHERE UUID IN (0x".implode(', 0x', $uuids).")");
				}

				$count_redoIDs += count($redoIDs);
			}
			
			$this->dbPingReconnect('migration');
			$this->dbPingReconnect('retention');
			LogMessage($lastID, '------------------------------------ LastID');
		}

		LogMessage($count_redoIDs, 'COUNT(redoIDs)');
		LogMessage($count_setDoneIDs, 'COUNT(setDoneIDs)');
		LogMessage('Done................................');
	}
}


VerifyMigrationApp::getInstance(cli::get('--conf', CONF_FILE))->run();

