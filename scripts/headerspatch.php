#!/usr/bin/php
<?php

/**
 * ArchivingHeadersPatchApp
 *
 * PHP version 5
 *
 * @category Library
 * @package  ClassAutoLoader
 * @author   Dino Korah <dino.korah@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 *
 */
define('APP_NAME_', 'rm-archiving-headerspatch');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
// define('RM_CLI_APP', true);

require_once 'rmapp/class.BaseApp.php';
require_once "s3/S3.php";

/**
* ArchivingHeadersPatchApp
*
* @category  ArchivingHeadersPatchApp
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingHeadersPatchApp extends BaseApp
{
	const BATCH_SIZE = 5;
	
	private static function getFileTypes($types_str)
	{
		$ret = array();

		if (0 === strcasecmp($types_str, 'all'))
			return array(Archiving::FILE_TYPE_CDR, Archiving::FILE_TYPE_RECORDING, Archiving::FILE_TYPE_SMS);
		
		foreach (preg_split('/[,;] */', $types_str) as $type)
			if ($type = Archiving::GetFileType($type))
				$ret[] = $type; // anything not UNKNOWN
		
		return array_unique($ret);
	}

	private static function getOrgIds($str)
	{
		$ret = array();
		
		$dbc = MySQLiConnection::Get('retention_ro');
		if (strtolower($str) == 'all')
		{
			$where = "TABLE_NAME LIKE 'Usage_%'";
		}
		else
		{
			$orgs = preg_split('/[,;] */', $str);
			$where = sprintf("TABLE_NAME RLIKE 'Usage_(%s)$'", implode('|', $orgs));
		}
		
		$dbc->query(
				"SELECT SUBSTRING(TABLE_NAME FROM 7) `OrgID` FROM `information_schema`.TABLES WHERE TABLES.TABLE_SCHEMA='ArchivingRetention' AND {$where}"
				)->for_each(
						function(MySQLiResult_Row $row) use (&$ret)
						{
							$ret[] = (int)$row->OrgID;
						});
	
		return $ret;
	}
	
	/**
	 * run
	 *
	 * @return none
	 */
	public function run()
	{
		$maxId = 0;
		
		$config = $this->conf;
		
		$filetypes = self::getFileTypes(cli::get('--types', $config->get('subset.file_types', 'cdr')));
		if (!count($filetypes))
			throw new Exception('Invalid file types specified');

		$orgIds = self::getOrgIds(cli::get('--orgs', $config->get('subset.org_ids', '1')));
		if (!count($orgIds))
			throw new Exception('Invalid OrgID(s) specified');
		
		$authHash = cli::get('--auth-hash', $config->get('api.authorization_hash', false));
		if (!$authHash)
			throw new Exception('No authorization hash specified');
		
		$absoluteStopDate = cli::get('--absolute-stop-date', $config->get('subset.absolute_stop_date', '2012-02-15'));
		if (!$absoluteStopDate)
			throw new Exception('No absolute stop date not specified');
				
		$startDate = cli::get('--start-date', $config->get('subset.start_date', null));
		$endDate = cli::get('--end-date', $config->get('subset.end_date', null));
		
		$authorization = null;
		$coreapi = $config->get('api.base_url', 'http://api.redmatter.com');
		$stream = HTTPStream::Make('GET', "{$coreapi}/auth/{$authHash}");
		if ($stream && $stream->httpCode == 201)
		{
			$xpath = new XPath($stream->getContents());
			$authorization = $xpath('/Success/ResourceID');
			LogVerbose('Authenticated');
		}
		
		unset($stream);
		
		if (!$authorization)
			throw new Exception('Failed to authenticate using the credentials given');
		
		$authorization_sentry = new ScopeSentry(
				/**
				 * Cleanup function for auth
				 */
				function() use ($authorization, $coreapi)
				{
					$stream = HTTPStream::Make('DELETE', "{$coreapi}/auth", null, array('Authorization'=>$authorization));
					LogVerbose('Auth deleted', $authorization);
				});
		
		$where = array();
		
		$where_date = array();
		if (!is_null($startDate))
			$where_date[] = "retention.CreationDate >= '{$startDate}'";
		if (!is_null($endDate))
			$where_date[] = "retention.CreationDate <= '{$endDate}'";
		if (count($where_date))
			$where[] = (count($where_date) > 1)? sprintf('( %s )', implode(' AND ', $where_date)): $where_date[0];
		
		$where[] = sprintf('retention.Type %s ( %s )', ((count($filetypes) > 1)? 'IN': '='), ((count($filetypes) > 1)? implode(', ', $filetypes): $filetypes[0]));

		$where = implode(' AND ', $where);
		
		$dbc = MySQLiConnection::Get('retention_ro');

		foreach ($filetypes as $filetype)
		{
			foreach ($orgIds as $orgId)
			{
				if (($filetype === Archiving::FILE_TYPE_CDR) && ($orgId !== 1))
					continue;

				$maxId = 0;
				
				do
				{
					$rows = 0;
					
					$dbc->query(
							"SELECT ID, IF(Format & (0x00f0 << 8), 'true', 'false') Encrypted, IF(Format & (0x000f << 8), 'true', 'false') Compressed, Format & 0xff SubType, ".
							"Type, PolicyID, PolicyVersion, CreationDate, RetentionMinDate, RetentionMaxDate, EndpointID, ".
							"LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID`, ".
							"LCASE(CONCAT(HEX(SUBSTR(LegUUID, 1, 4)),'-',HEX(SUBSTR(LegUUID, 5, 2)),'-',HEX(SUBSTR(LegUUID, 7, 2)),'-',HEX(SUBSTR(LegUUID, 9, 2)),'-',HEX(SUBSTR(LegUUID, 11)))) `LegUUID`, ISNULL(retention.LegUUID) `LegUUIDNull`".
							"FROM ArchivingRetention.Retention_{$orgId} retention WHERE ID > {$maxId} AND retention.CreationDate < '{$absoluteStopDate}' AND {$where} LIMIT ".self::BATCH_SIZE
							)->for_each(
									/**
									 * Record handler lambda
									 * 
									 * @param MySQLiResult_Row $row one record
									 */
									function(MySQLiResult_Row $row) use ($orgId, $coreapi, $authorization, &$rows, &$maxId)
									{
										$rows++;
										$maxId = $row->ID;
										
										$identifier = sprintf('%s/%d/%s', Archiving::GetFileTypeStr($row->Type), $orgId, $row->UUID);
										$endpoint = ArchivingEndpoint::Get(array($coreapi, $authorization), $orgId, $row->EndpointID, true);
										
										LogDebug($maxId, $identifier);
										
										if ($endpoint->type != 's3')
										{
											LogMessage('Ignoring object from non-S3 endpoint', $identifier);
											return;
										}

										$s3 = new S3($endpoint->accessKey, $endpoint->secretKey);
										
										$inf = $s3->getObjectInfo($endpoint->bucket, $identifier);
										LogDebug(var_export($inf, true), 'Metadata from S3 for '.$identifier);
										$real_size = $inf['size'];
										
										$headers = array(
												'creation-date' => $row->CreationDate,
												'file-type' => Archiving::GetSubFormatStr($row->SubType),
												'file-type-is-compressed' => $row->Compressed,
												'file-type-is-encrypted' => $row->Encrypted,
												'policy-id' => $row->PolicyID,
												'policy-version' => $row->PolicyVersion,
												'real-size' => $real_size,
												'retention-max-date' => $row->RetentionMaxDate,
												'retention-min-date' => $row->RetentionMinDate,
												'leg-uuid' => ($row->LegUUIDNull == 1)? '': $row->LegUUID
											);
										
										$header_mismatch = false;
										foreach ($headers as $hKey => $hValue)
										{
											$amzKey = sprintf('x-amz-meta-%s', $hKey);
											$amzValue = isset($inf[$amzKey])? $inf[$amzKey]: null;
											if (is_null($amzValue) || ($amzValue != $hValue))
											{
												$header_mismatch = true;
												LogMessage("Metadata mismatch: {$hKey}: (s3) '{$amzValue}' != (db) '{$hValue}'", $identifier);
											}
										}
										
										if ($header_mismatch)
										{
											$retry = 5;
											$delay = 0;
											$copy_ok = null;
											do {
												if (!is_null($copy_ok))
													sleep($delay);
											
												if (false === ($copy_ok = $s3->copyObject($endpoint->bucket, $identifier, $endpoint->bucket, $identifier, S3::ACL_PRIVATE, $headers, array('Content-Type'=>'binary/octet-stream'))))
												{
													if ($retry)
													{
														$delay = pow(6 - $retry, 2);
														LogWarning("Error copying new metadata; retrying in $delay seconds", $identifier);
													}
													else
														LogError('Error copying new metadata; giving up!', $identifier);
												}
											} while ($retry-- && !$copy_ok);

											LogMessage(sprintf('outcome:%s', $copy_ok? 'ok': 'failed'), $identifier);
										}
										else
											LogMessage('No mismatch in metadata', $identifier);
										
										unset($s3);
									});
				} while ($rows == self::BATCH_SIZE);
			}
		}
	}
}

try
{
	if (true === cli::get('--help'))
	{
		$self = basename(__FILE__, '.php');
		echo <<<HELP

Archiving Metadata Headers Patcher
$self --conf=/path/to/run.conf [--orgs=<all|Org1[,Org2,...]>] [--types=<all|Type1[,Type2,...]>]
        [--start-date=<YYYY-MM-DD>] [--end-date=<YYYY-MM-DD>] 

    --orgs=<all|Org1[,Org2,...]>
        Config: subset.org_ids

    --types=<all|Type1[,Type2,...]>
        Config: subset.file_types

    --start-date=<YYYY-MM-DD>
        Config: subset.start_date

    --end-date=<YYYY-MM-DD>
        Config: subset.end_date

HELP;
		exit(0);
	}

	if (false === ($config_file = cli::get('--conf', false)))
		throw new Exception('No configuration given; use --conf option');

	ArchivingHeadersPatchApp::getInstance($config_file)->run();
	exit(0);
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}
