#!/usr/bin/php
<?php
//NoSniff

/**
 * ArchivingExtractApp
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
define('APP_NAME_', 'rm-archiving-extract');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
// define('RM_CLI_APP', true);

require_once 'rmapp/class.BaseApp.php';

define('ARCHIVING_FILE_TYPE_UNKNOWN', 0);
define('ARCHIVING_FILE_TYPE_CDR', 1);
define('ARCHIVING_FILE_TYPE_RECORDING', 2);
define('ARCHIVING_FILE_TYPE_SMS', 3);

/**
* ArchivingExtractApp
*
* @category  ArchivingExtractApp
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingExtractApp extends BaseApp
{
	/**
	 * run
	 *
	 * @return none
	 */
	public function run()
	{
		$maxId = 0;
		
		$config = $this->conf;
		
		if (false === ($orgId = cli::get('--org', $config->get('extract.org_id', false))))
			throw new Exception('OrgID not given');
		
		if (false === ($caseRef = cli::get('--case-ref', $config->get('extract.case_reference', false))))
			throw new Exception('Case reference not given');

		if (false === ($outDir = cli::get('--out-dir', $config->get('extract.output_dir', false))))
			throw new Exception('Output dir not given');
		
		if (false === ($username = cli::get('--user', $config->get('extract.username', false))))
			throw new Exception('Username not given');

		if (false === ($password = cli::get('--password', $config->get('extract.password', false))))
			throw new Exception('Username not given');
		
		$passcode = cli::get('--passcode', null);
		
		$startDate = cli::get('--start-date', $config->get('extract.start_date', null));
		$endDate = cli::get('--end-date', $config->get('extract.end_date', null));
		
		$authorization = null;
		$coreapi = $config->get('coreapi.url', 'http://api.redmatter.com');
		$hash = urlencode(sha1(utf8_encode(mb_strtolower($username, 'UTF-8'))).sha1(utf8_encode($password)).(isset($passcode)? $passcode: ''));
		$stream = HTTPStream::Make('GET', "{$coreapi}/auth/{$hash}");
		if ($stream && $stream->httpCode == 201)
		{
			$xpath = new XPath($stream->getContents());
			$authorization = $xpath('/Success/ResourceID');
			LogVerbose('Authenticated');
		}
		elseif ($stream)
		{
			LogDebug($stream->getContents(), $stream->httpCode);
		}
		
		unset($stream);
		unset($username);
		unset($password);
		
		if (!$authorization)
			throw new Exception('Failed to authenticate using the credentials given');
		
		$authorization_sentry = new ScopeSentry(
			/**
			 * Cleanup function for auth
			 */
			function() use ($authorization, $coreapi)
			{
				$stream = HTTPStream::Make('DELETE', "{$coreapi}/auth", null, array('Authorization'=>$authorization));
				LogDebug($stream->getContents(), $stream->httpCode);
				LogVerbose('Auth deleted', $authorization);
			}
		);
		
		ArchiveD::Init(self::getConf(), $authorization, $caseRef);
		
		if ((!is_dir($outDir) && (false === mkdir($outDir, 0755, true))) || !is_writable($outDir))
			throw new Exception("Failed to find / make usable output dir:$outDir");
		
		$where = '';
		if (!is_null($startDate))
			$where = "retention.CreationDate >= '{$startDate}'";
		if (!is_null($endDate))
			$where = ($where? "{$where} AND ": '')."retention.CreationDate <= '{$endDate}'";
		
		$where = $where? "AND ( {$where} )": '';
		
		$dbc = MySQLiConnection::Get('retention_ro');
		do
		{
			$rows = 0;
			$dbc->query(
				"SELECT ID, Type, LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID` ".
					"FROM ArchivingRetention.Retention_{$orgId} retention WHERE ID > {$maxId} {$where} LIMIT 100"
			)->for_each(
				/**
				 * Handler for each row
				 * 
				 * @param MySQLiResult_Row $row row from the result set
				 */
				function(MySQLiResult_Row $row) use (&$maxId, &$rows, $outDir, $orgId)
				{
					$rows++;
					$maxId = $row->ID;
					
					if ($row->Type == ARCHIVING_FILE_TYPE_RECORDING)
					{
						try
						{
							if ($stream = ArchiveD::Fetch('recording', $row->UUID))
							{
								$stream->spitContents($outDir.DIRECTORY_SEPARATOR.$row->UUID.'.wav');
								/*
								if ($stream = ArchiveD::Head('recording', $row->UUID))
									$stream->spitContents($outDir.DIRECTORY_SEPARATOR.$row->UUID.'.meta');
								*/
							}
						}
						catch (Exception $e)
						{
							LogWarning($e, "recording/{$orgId}/{$row->UUID}");
						}
					}
				}
			);
		} while($rows == 100);
		
		exit(0);
	}
}

try
{
	ArchivingExtractApp::getInstance(CLI::get('--conf', getenv('HOME').'/archiving-extract.conf'))->run();
}
catch (Exception $e)
{
	LogWarning($e);
}
