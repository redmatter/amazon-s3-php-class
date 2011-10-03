<?php
/**
 * ArchivingChecker
 *
 * PHP version 5
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
* ArchivingChecker
*
* @category  Library
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 RedMatter
* @license   http://www.redmatter.com/license private
* @link      http://www.redmatter.com
*/
class ArchivingChecker
{
	protected $fileType;
	protected $fileTypeName;

	/**
	 * static helper
	 *
	 * @param int    $fileType file-type
	 * @param int    $rmOrgID  org
	 * @param string $uuid     uuid
	 *
	 * @return status
	 */
	public static function Check($fileType, $rmOrgID, $uuid)
	{
		try
		{
			$checker = new ArchivingChecker($fileType, $rmOrgID, $uuid);
			$checker->_check();
			return 'OK';
		}
		catch (Exception $e)
		{
			return $e->getMessage();
		}
	}

	/**
	 * Ctor
	 *
	 * @param int    $fileType file-type constant
	 * @param int    $rmOrgID  orgId
	 * @param string $uuid     uuid
	 */
	private function __construct($fileType, $rmOrgID, $uuid)
	{
		$this->fileType = $fileType;
		$this->rmOrgID = $rmOrgID;
		$this->uuid = strtolower($uuid);

		$this->uuid_hex = str_replace('-', '', $this->uuid);

		switch ($this->fileType)
		{
			default:
			case ARCHIVING_FILE_TYPE_UNKNOWN:
				$this->fileTypeName = 'unknown';
				break;

			case ARCHIVING_FILE_TYPE_CDR:
				$this->fileTypeName = 'cdr';
				break;

			case ARCHIVING_FILE_TYPE_RECORDING:
				$this->fileTypeName = 'recording';
				break;
		}
	}

	/**
	 * attempts to loads the file; throws on not found or access permission violations
	 *
	 * @return none
	 */
	private function _check()
	{
		if (!$this->rmOrgID)
		throw new Exception("No rmOrgID given");

		// if you need to inspect any of the temporary files that get created
		// during the serving of an archived file, set this to true
		$keepTemp = ('true' === config::get('app.keep_temp_files', 'false'));

		$retention = ArchivingRetention::Find(MySQLiConnection::Get('retention_ro'), $this->rmOrgID, $this->fileType, $this->uuid_hex);
		$endpoint = ArchivingEndpoint::Get(MySQLiConnection::Get('coreapi_ro'), $this->rmOrgID, $retention->EndpointID);

		$s3 = new S3($endpoint->accessKey, $endpoint->secretKey);
		$identifier = "{$this->fileTypeName}/{$this->rmOrgID}/{$this->uuid}";
		LogMessage("bucket:{$endpoint->bucket} key:$identifier: Lookup on S3");
		if (($info = $s3->getObjectInfo($endpoint->bucket, $identifier)) !== false)
		{
			LogMessage($info, "$identifier found on S3");

			$s3Size = $info['size'];

			$resPathS3 = new TempPath($identifier.'.', null, $keepTemp);
			if (false === $s3->getObject($endpoint->bucket, $identifier, $resPathS3->getPath()))
			throw new Exception("Failed to retrieve file");

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
					'--key', base64_encode($endpoint->encryptionKey),
					'--log.file_path', 'syslog',
					'--log.level', 6,
					'--log.syslog-tag', APP_NAME_,
					'--temp-dir', config::get('app.TempDir', '/tmp')
				)
			);
			LogMessage($command, 'command');
			$pHandle = proc_open($command, $descriptorSpec, $pipes);
			if (false == $pHandle)
			throw new Exception('Failed opening archiving-convert');

			$procStatus = proc_get_status($pHandle);
			$pid = $procStatus['pid'];
			// work around for http://bugs.php.net/bug.php?id=39992
			posix_setpgid($pid, $pid);

			$timeout = (int)(5/*s*/ + ($s3Size / (5 * 1024 * 1024))) * 10;
			LogVerbose($timeout, "Set convert-timeout (seconds*10)");

			while ($procStatus['running'] && $timeout--)
			{
				usleep(100/*ms*/ *1000);
				$procStatus = proc_get_status($pHandle);
			}

			fclose($pipes[0]);
			fclose($pipes[1]);
			fclose($pipes[2]);

			if (!$timeout && $procStatus['running'])
			{
				LogWarning("Archiving-convert timed-out; killing it");

				// proc_terminate($pHandle);
				// work around for http://bugs.php.net/bug.php?id=39992
				posix_kill(-$pid, 9);
			}
			elseif (0 !== $procStatus['exitcode'])
			{
				LogError("Failed to convert file copied from S3 - exit-code:{$procStatus['exitcode']}");
				throw new Exception("Failed 'convert' of file");
			}
			else
			{
				LogVerbose("Successfully converted the file");
			}
		}
		else
		LogMessage("$identifier NOT found on EndPoint:{$endpoint->EndpointID}");
	}
}
