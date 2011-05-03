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
		echo "verify-migration.php --s3-access-key=<S3AccessKey> --s3-secret-key=<S3SecretKey> [--s3-bucket=<S3Bucket>] --conf=<ConfFile default:verify-migration.conf> [--check-orgs=<OrgList>]\n";
		
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
		
		$s3AccessKey = cli::get('--s3-access-key');
		$s3SecretKey = cli::get('--s3-secret-key');
		$s3Bucket = cli::get('--s3-bucket', $conf->get('s3.bucket', 'rm-archiving'));
		if (!isset($s3AccessKey, $s3SecretKey))
			$this->showUsage(1, "S3 Credentials NOT given");
			
		$checkOrgs = cli::get('--check-orgs');
			
		$s3 = new S3($s3AccessKey, $s3SecretKey);
		
		$lastID = 0;
		
		while ($res = $migDb->Execute("SELECT ID, LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID`, HEX(UUID) `UUID_HEX`, OrgID FROM Migration WHERE Status='migrated' AND ID > {$lastID} ".(isset($checkOrgs)? " AND OrgID IN ({$checkOrgs})": '')." LIMIT 100"))
		{
			if (!$res->RecordCount())
				break;
				
			$redoIDs = array();
				
			while ($row = $res->FetchRow())
			{
				$s3Key = "recording/{$row[3]}/{$row[1]}";
				if (false !== $s3->getObjectInfo($s3Bucket, $s3Key, false))
				{
					if($retRes = $retDb->Execute("SELECT ID FROM Retention_{$row[3]} WHERE UUID=0x{$row[2]}"))
					{
						if (!$retRes->RecordCount())
						{
							LogMessage('Found Retention Record NOT found', "Org:{$row[3]} UUID:{$row[1]}");
							$redoIDs[] = $row[0];
						}
						else
							LogMessage('Found Retention Record', "Org:{$row[3]} UUID:{$row[1]}");
					}
					
					$retRes->Close();
				}
				
				$lastID = $row[0];
			}
			
			$migDb->Execute("UPDATE Migration SET Status='pending' WHERE ID IN (".implode(',', $redoIDs).")");
			
			$this->dbPingReconnect('migration');
			$this->dbPingReconnect('retention');
		}
	}	
}


VerifyMigrationApp::getInstance(cli::get('--conf', CONF_FILE))->run();

