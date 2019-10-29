#!/usr/bin/php
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
define('APP_NAME_', 'rm-archiving-export-any');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);

require_once "s3/S3.php";
require_once 'rmapp/class.BaseApp.php';

/**
 * ArchivingExportAny
 *
 * @category  Library
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class ArchivingExportAny
{
    protected $fileType;
    protected $fileTypeName;
    protected $fileTypeExtension;
    protected $rmOrgID;
    protected $uuid;
    protected $uuid_hex;
    private $conf;

    /**
     * static helper
     *
     * @param ConfigMap $conf
     * @param int $fileType file-type
     * @param int $rmOrgID  org
     * @param string $uuid     uuid
     * @param string $path
     *
     * @return bool true on success
     */
    public static function ExportToPath($conf, $fileType, $rmOrgID, $uuid, $path)
    {
        $checker = new ArchivingExportAny($conf, $fileType, $rmOrgID, $uuid);
        return $checker->_exportToPath($path);
    }

    /**
     * Ctor
     *
     * @param ConfigMap $conf
     * @param int $fileType file-type constant
     * @param int $rmOrgID  orgId
     * @param string $uuid     uuid
     */
    private function __construct($conf, $fileType, $rmOrgID, $uuid)
    {
        $this->conf = $conf;
        $this->fileTypeName = Archiving::GetFileTypeStr($fileType);
        $this->fileType = $fileType;
        $this->rmOrgID = $rmOrgID;
        $this->uuid = strtolower($uuid);
        $this->uuid_hex = str_replace('-', '', $this->uuid);
    }

    /**
     * attempts to loads the file; throws on not found or access permission violations
     *
     * @param string $path
     *
     * @return bool true on success
     */
    private function _exportToPath($path)
    {
        $filePath = $path.DIRECTORY_SEPARATOR.$this->uuid.'.'.Archiving::GetFileTypeExtension($this->fileType);

        $identifier = "{$this->fileTypeName}/{$this->rmOrgID}/{$this->uuid}";
        $lc = new SysLogContext($identifier);

        try
        {
            $api = API::GetToken(true);

            $keepTemp = ('true' === $this->conf->get('app.keep_temp_files', 'false'));

            $retention = ArchivingRetention::Find(MySQLiConnection::Get('retention_ro'), $this->rmOrgID, $this->fileType, $this->uuid_hex);
            $endpoint = ArchivingEndpoint::Get(array($api->URL, $api->Token), $this->rmOrgID, $retention->EndpointID);
            $s3 = new S3($endpoint->accessKey, $endpoint->secretKey);

            $resPathS3 = new TempPath($identifier.'.', null, $keepTemp);

            $retry = 0;
            /** @noinspection PhpUnusedLocalVariableInspection */
            $resS3 = false;
            while (true)
            {
                $s3_start_time = microtime(true);
                if (false !== ($resS3 = $s3->getObject($endpoint->bucket, $identifier, $resPathS3->getPath())))
                {
                    $lc->LogMessage('Downloaded from S3');
                    break;
                }

                if ($retry++ <= 3)
                {
                    $lc->LogWarning('Download from S3 failed; retrying...');
                    sleep(1);
                    continue;
                }
                else
                {
                    $lc->LogError('Download from S3 failed');
                    throw new Exception("Failed to retrieve file");
                }
            }

            $resPath = new TempPath($identifier.'.', null, $keepTemp);

            $descriptorSpec = array(
                0 => array('pipe', 'r'),
                1 => array('pipe', 'w'),
                2 => array('pipe', 'w')
            );

            $command = implode(
                ' ',
                array(
                    '/usr/bin/archiving-convert',
                    '--source', $resPathS3->getPath(),
                    '--target', $resPath->getPath(),
                    '--key', escapeshellarg($endpoint->encryptionKey),
                    '--log.file_path', 'syslog',
                    '--log.level', 6,
                    '--log.syslog-tag', APP_NAME_,
                    '--temp-dir', $this->conf->get('app.TempDir', '/tmp')
                )
            );

            $lc->LogMessage('Decrypting... ');
            $pHandle = proc_open($command, $descriptorSpec, $pipes);
            if (false == $pHandle)
                throw new Exception('Failed opening archiving-convert');

            $procStatus = proc_get_status($pHandle);
            $pid = $procStatus['pid'];
            // work around for http://bugs.php.net/bug.php?id=39992
            posix_setpgid($pid, $pid);

            $timeout = 30000; /* 30000 * 10ms (5mins)*/
            $lc->LogVerbose($timeout, "Set convert-timeout (seconds*100)");

            while ($procStatus['running'] && $timeout--)
            {
                usleep(10/*ms*/ * 1000);
                $procStatus = proc_get_status($pHandle);
            }

            fclose($pipes[0]);
            fclose($pipes[1]);
            fclose($pipes[2]);

            if (!$timeout && $procStatus['running'])
            {
                $lc->LogWarning("Archiving-convert timed-out; killing it");

                // work around for http://bugs.php.net/bug.php?id=39992
                posix_kill(-$pid, 9);

                throw new Exception("Failed 'convert' of file (timeout)");
            }
            elseif (0 !== $procStatus['exitcode'])
            {
                $lc->LogError("Failed to convert file copied from S3 - exit-code:{$procStatus['exitcode']}");
                throw new Exception("Failed 'convert' of file");
            }
            else
            {
                $lc->LogMessage("Successfully converted the file");
            }

            copy($resPath->getPath(), $filePath);
            return true;
        }
        catch(Exception $e)
        {
            $lc->LogError('Exception: '.$e->getMessage());
        }

        $lc->LogError('Failed');
        return false;
    }
}

/**
* ArchivingExporterApp
*
* @category  ArchivingExporterApp
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingExporterApp extends BaseApp
{
    private $_infp;
    private $_outPath;

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

		$this->_outPath = CLI::get('--out-dir', getcwd());
	}

    /**
	 * run
	 *
	 * @return none
	 */
	public function run()
	{
        API::Init($this->getConf()->get('coreapi.url', 'http://cdr.coreapi.{hapi:SearchDomain}'), new ArrayStorage(),
            APP_NAME_ . ':1.0');
        API::LoginBasicAuth($this->getConf()->get('coreapi.authorization_creds', false), null/*mfa*/,
            true/*re_authenticate*/);

		while ($row = fgetcsv($this->_infp))
		{
			$uuid = trim($row[0]);
			$rmOrgID = isset($row[1])? $row[1]: CLI::get('--org', 1);
			$fileType = Archiving::GetFileType(isset($row[2])? $row[2]: CLI::get('--filetype', Archiving::FILE_TYPE_STR_CDR));



			if (!ArchivingExportAny::ExportToPath($this->getConf(), $fileType, $rmOrgID, $uuid, $this->_outPath)) {
			    if (in_array($fileType, array(Archiving::FILE_TYPE_PCAP, Archiving::FILE_TYPE_PCAP_SIP_ONLY))) {
                    $otherType = $fileType === Archiving::FILE_TYPE_PCAP? Archiving::FILE_TYPE_PCAP_SIP_ONLY: Archiving::FILE_TYPE_PCAP;
                    LogMessage(Archiving::GetFileTypeStr($fileType).' Failed; trying '.Archiving::GetFileTypeStr($otherType));
                    ArchivingExportAny::ExportToPath($this->getConf(), $otherType, $rmOrgID, $uuid, $this->_outPath);
                }
            }
		}

		exit(0);
	}
}

ArchivingExporterApp::getInstance(CLI::get('--conf', getenv('PWD').'/export-any.conf'))->run();
