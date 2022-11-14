#!/usr/bin/php
<?php

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
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);
// define('RM_CLI_APP', true);

require_once __DIR__ . '/vendor/autoload.php';

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
	 * @throws Exception
	 *
	 * @return void
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
			throw new Exception('Password not given');

		$extractTypes = cli::get('--type', $config->get('extract.type', 'recording'));
		$extractTypes = preg_split('/[;, ]+/', $extractTypes);
		$extractTypes = array_map(
			function($str)
			{
				$int = Archiving::GetFileType($str);
				if ($int === Archiving::FILE_TYPE_UNKNOWN)
					throw new Exception('Unknown file-type '.$str);

				return $int;
			},
			$extractTypes
		);

		if (empty($extractTypes))
			throw new Exception('No valid file-types specified');

		$passcode = cli::get('--passcode', null);

		$startDate = cli::get('--start-date', $config->get('extract.start_date', null));
		$endDate = cli::get('--end-date', $config->get('extract.end_date', null));

		$authorization = null;
		$coreapi = $config->get('coreapi.url', 'http://cdr.coreapi.redmatter.com');
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

		/** @noinspection PhpUnusedLocalVariableInspection */
		$authorization_sentry = new ScopeSentry(
			// Cleanup function for auth
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

		$where = array();
		$where[] = "`retention`.`Type` IN (" . implode(', ', $extractTypes) . ")";

		if (!is_null($startDate))
			$where[] = "`retention`.`CreationDate` >= '{$startDate}'";
		if (!is_null($endDate))
			$where[] = "`retention`.`CreationDate` <= '{$endDate}'";

		$where = implode(' AND ', $where);

		$dbc = MySQLiConnection::Get('retention_ro');
		do
		{
			$rows = 0;
			$query = "SELECT ID, Type, LCASE(CONCAT(HEX(SUBSTR(UUID, 1, 4)),'-',HEX(SUBSTR(UUID, 5, 2)),'-',HEX(SUBSTR(UUID, 7, 2)),'-',HEX(SUBSTR(UUID, 9, 2)),'-',HEX(SUBSTR(UUID, 11)))) `UUID` ".
				"FROM ArchivingRetention.Retention_{$orgId} retention WHERE `retention`.`ID` > {$maxId} AND {$where} LIMIT 100";
			$dbc->query($query)
				->for_each(
					function (MySQLiResult_Row $row) use (&$maxId, &$rows, $outDir, $orgId)
					{
						$rows++;

						/** @var MySQLiResult_Row|stdClass $row */
						$maxId = $row->ID;

						$file_type_str = Archiving::GetFileTypeStr($row->Type);

						try
						{
							if ($stream = ArchiveD::Fetch($file_type_str, $row->UUID))
							{
								$stream->spitContents(
									$outDir . DIRECTORY_SEPARATOR . $row->UUID .
									Archiving::GetFileTypeExtension($row->Type)
								);
								/*
								if ($stream = ArchiveD::Head('recording', $row->UUID))
									$stream->spitContents($outDir.DIRECTORY_SEPARATOR.$row->UUID.'.meta');
								*/
							}
						}
						catch (Exception $e)
						{
							LogWarning($e, "{$file_type_str}/{$orgId}/{$row->UUID}");
						}
					}
				);
		}
		while ($rows == 100);

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
