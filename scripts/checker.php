#!/usr/bin/php54
<?php

/**
 * ClassAutoLoader
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
define('APP_NAME_', 'rm-archiving-checker');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);

require_once __DIR__ . '/vendor/autoload.php';

define('ARCHIVING_FILE_TYPE_UNKNOWN', 0);
define('ARCHIVING_FILE_TYPE_CDR', 1);
define('ARCHIVING_FILE_TYPE_RECORDING', 2);

/**
* ArchivingCheckerApp
*
* @category  ArchivingCheckerApp
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingCheckerApp extends BaseApp
{
	private $_lastDate = false;
	private $_lastTime = false;

	/**
	 * Ctor
	 *
	 * @param string $configfile conf
	 *
	 * @throws Exception
	 */
	public function __construct($configfile)
	{
		parent::__construct($configfile);

		if (false === ($this->_infp = fopen(CLI::get('--in', 'php://stdin'), 'r')))
			throw new Exception('Unable to open input');

		if (false === ($this->_outfp = fopen(CLI::get('--out', 'php://stdout'), 'w')))
			throw new Exception('Unable to open output');
	}

	/**
	 * run
	 *
	 * @return none
	 */
	public function run()
	{
		while ($row = fgetcsv($this->_infp))
		{
			$uuid = trim($row[0]);
			if (empty($uuid))
				break;

			$rmOrgID = isset($row[1])? $row[1]: 1;
			$fileType = isset($row[2])? $row[2]: 1;
			$date = isset($row[3])? $row[3]: 'unknown-date';
			$host = isset($row[4])? $row[4]: 'unknown-host';

			$res = ArchivingChecker::Check($fileType, $rmOrgID, $uuid);
			if (false === fputcsv($this->_outfp, array($uuid, $rmOrgID, $fileType, $date, $host, $res['message'], $res['time-taken'], $res['time-taken-s3'], $res['time-taken-convert'], $res['retry-s3'], $res['bytes-s3'], $res['bytes'])))
			{
				LogError('Failed writing output');
				exit(1);
			}
		}

		exit(0);
	}
}

ArchivingCheckerApp::getInstance(CLI::get('--conf', getenv('PWD').'/checker.conf'))->run();
