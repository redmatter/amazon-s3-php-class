<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway
{

use rmarchgateway\resources\Storage;

use rmarchgateway\renderables\StorageError;

use fluid\Request;

use fluid\RenderSource;

use fluid\HttpException;

use fluid\Http404NotFound;

use \config;

/**
 * Storage Item
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class StorageItem implements RenderSource
{
	const LOCK_STALE_SECONDS = 3600;
	const LOCK_MODE_RW = 'rw';
	const LOCK_MODE_RO = 'ro';
	
	// Usage modes
	const USAGE_HEAD = 'head';
	const USAGE_GET = 'get';
	const USAGE_PUT = 'put';
	const USAGE_DELETE = 'delete';
	const USAGE_MULTI_DELETE = 'multi_delete';
	
	protected $usageMode;
	
	protected $org;
	protected $type;
	protected $uuid;
	protected $metadata;
	protected $given_md5_base64;
	
	public $md5;
	
	public $id;
	
	protected $path;
	protected $path_meta;
	protected $final_path;
	protected $final_path_meta;
	protected $meta_file_content;
	protected $persist = false;
	protected $stream = null;
	protected $commited = null;
	protected $lock_path = false;
	
	protected $stat_path;
	protected $stat_val;
	
	protected $lc;
	
	/**
	 * Ctor
	 * Also performs any checking that could cause an exception to be thrown, generally to do with file access.
	 * The checking that takes place depends on the command the StorageItem has been instantiated to implement.
	 * HEAD: Checks that the target file as well as its .meta file are present and locks them with RO.
	 * GET: Same checking as HEAD.
	 * PUT: Checks that the target file as well as its .meta file are either not present, or are but can be locked with RW.
	 * DELETE: Checks that the target file as well as its .meta file are present and locks them with RW.
	 * 
	 * The constructor determines its usage mode from the passed $mode variable.
	 * 
	 * @param string   $usage_mode   Function which will be performed using this storage item.
	 * @param string   $bucket       S3 Bucket
	 * @param string   $org          Organisation
	 * @param string   $type         Resource type
	 * @param string   $uuid         Unique Identifier (Key)
	 * @param array    $metadata     Metadata to be stored during PUT
	 * @param resource $stream       Resource stream (the file to be PUT)
	 * @param string   $md5_base64   The MD5 hash of the resource
	 * 
	 * @throws HttpException
	 * @throws StorageError
	 * @throws Exception
	 */
	public function __construct($usage_mode, $bucket, $org, $type, $uuid, $metadata=null, $stream=null, $md5_base64=null)
	{
		// Initialise all members variables from constructor parameters. 
		$this->usageMode = $usage_mode;

		$this->org = $org;
		$this->type = $type;
		$this->uuid = $uuid;

		$this->id = "/{$bucket}/{$type}/{$org}/{$uuid}";

		$this->lc = new \SysLogContext($this->id);

		$this->final_path = $this->path = config::get('storage.base_path', '/tmp').DIRECTORY_SEPARATOR.$org.DIRECTORY_SEPARATOR.$type.DIRECTORY_SEPARATOR.self::UuidToPath($uuid);
		$this->final_path_meta = $this->path_meta = $this->path.'.meta';

		$this->stream = $stream;

		// Perform checking.
		
		// Call stat to get the attributes of the file paths specified, this will also tell us if the files exist.
		$path_exists = is_file($this->path);
		$path_meta_exists = is_file($this->path_meta);
		
		if ($path_exists === true)
		{
			$this->stat_path = @stat($this->path);
		}
		
		if ($path_meta_exists === true)
		{
			$this->stat_meta = @stat($this->path_meta);
		}
		
		// For the usage modes which require the files to exist, throw an error if the files don't exist.
		switch ($this->usageMode)
		{
			case self::USAGE_HEAD:
			case self::USAGE_GET:
			case self::USAGE_DELETE:
			{
				if (!$path_exists)
				{
					LogError('path does not exist', $this->id);
					throw new StorageError(StorageError::NoSuchKey, $this->id, Request::Get('id'));
				}
				break;
			}
			default:
				break;
		}
		
		// If we're servicing a HEAD or GET request, lock the file for read-only access.  This will throw an exception if the
		// file can't be locked.  Also get the MD5 for the file.
		if (($this->usageMode == self::USAGE_HEAD) || ($this->usageMode == self::USAGE_GET))
		{
			$this->Lock(self::LOCK_MODE_RO);
			$this->md5 = md5_file($this->path, true);
			
			if ($path_meta_exists)
			{
				$this->meta_file_content = file_get_contents($this->path_meta);
			}
		}
		
		// If we're doing a GET, create the file stream.  Throw an exception if this fails.
		if ($this->usageMode == self::USAGE_GET)
		{
			if (false === ($this->stream = @fopen($this->path, 'rb')))
			{
				throw new Exception('Failed opening stream');
			}
		}
		
		// Perform the checks necessary for a PUT.
		if ($this->usageMode == self::USAGE_PUT)
		{
			if (is_null($stream))
			{
				LogError('Stream was NULL', $this->id);
				throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
			}
			
			if (is_null($md5_base64))
			{
				LogError('No MD5 given', $this->id);
				throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
			}

			if (is_null($metadata) || (is_array($metadata) && !count($metadata)))
			{
				LogError('No Metadata given', $this->id);
				throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
			}

			$this->metadata = $metadata;

			$this->given_md5_base64 = $md5_base64;

			// check if the file already exists
			if ($path_exists || $path_meta_exists)
			{
				$tmp = '.'.time().getmypid();

				if ($path_exists)
				{
					$this->path .= $tmp;
				}

				if ($path_meta_exists)
				{
					$this->path_meta .= $tmp;
				}
			}
			
			$this->Lock(self::LOCK_MODE_RW);
		}
	}
	
	/**
	 * getHttpResponseHead
	 * Returns an array containing the HTTP response code and message based on the usage mode.
	 * 
	 * @throws StorageError
	 * 
	 * @return array
	 */
	public function getHttpResponseHead()
	{
		switch ($this->usageMode)
		{
			case self::USAGE_HEAD:
			case self::USAGE_GET:
			case self::USAGE_PUT:
				return array('code' => '200', 'message' => 'OK');
			case self::USAGE_DELETE:
				return array('code' => '204', 'message' => 'NoContent');
			default:
			{
				LogError('Invalid usage mode', $this->id);
				throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
			}
		}
	}
	
	/**
	 * getHttpResponseParameters
	 * Returns an array containing the HTTP response parameters based on the usage mode.
	 *
	 * @throws StorageError
	 * @throws Exception
	 * 
	 * @return array
	 */
	public function getHttpResponseParameters()
	{
		$http_response_parameters = array();
	
		switch ($this->usageMode)
		{
			case self::USAGE_HEAD:
			{
				$http_response_parameters[] = $this->getContentLength();
				// Deliberate fall-through.  We want the above parameter in addition to the ones GET uses.
			}
			case self::USAGE_GET:
			{
				$http_response_parameters[] = $this->getLastModified();
				$http_response_parameters[] = $this->getETag();
				$http_response_parameters = array_merge($http_response_parameters, $this->getMetadata());
				$http_response_parameters[] = $this->getContentType();
				return $http_response_parameters;
			}
			case self::USAGE_PUT:
			{
				$http_response_parameters[] = $this->GetETag();
				return $http_response_parameters;
			}
			case self::USAGE_DELETE:
			{
				return $http_response_parameters;
			}
			default:
			{
				LogError('Invalid usage mode', $this->id);
				throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
			}
		}
	}
	
	/**
	 * renderContent
	 * Render the HTTP Body, the actual content of the file.
	 * If opening the output stream or copying to it fails, returns false.  Otherwise, returns true.
	 *
	 * @return bool
	 */
	public function renderContent()
	{
		$result = true;

		$out = fopen('php://output', 'wb');
		
		if ($out === false)
		{
			LogError('Could not open php://output', $this->id);
			return false;
		}
		
		if (stream_copy_to_stream($this->stream, $out) == 0)
		{
			LogError('copy_to_stream copied 0 bytes', $this->id);
			$result = false;
		}
		
		fclose($out);
		fclose($this->stream);
		
		return $result;
	}
	
	/**
	 * getContentLength
	 * Gets the content length based on the stat_path.
	 *
	 * @throws StorageError
	 *
	 * @return string
	 */
	protected function getContentLength()
	{
		if ($this->stat_path === false)
		{
			LogError('$this->stat_path is false', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}
		
		if (isset($this->stat_path['size']) === false)
		{
			LogError('$this->stat_path[\'size\'] is not set', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}

		return 'Content-Length: ' . $this->stat_path['size'];
	}

	/**
	 * getLastModified
	 * Get the last modified date of the file, formatted for returning in an HTTP response.
	 *
	 * @return string
	 */
	protected function getLastModified()
	{
		$lastModTime = new \DateTime('@'.$this->stat_path['mtime'], new \DateTimeZone('UTC'));
		return sprintf('Last-Modified: %s', $lastModTime->format(\DateTime::COOKIE));
	}

	/**
	 * getETag
	 * Get the ETag for an HTTP response, this is a hex representation of the MD5 of the file.
	 *
	 * @return string
	 */
	protected function getETag()
	{
		return sprintf('ETag: "%s"', bin2hex($this->md5));
	}
	
	/**
	 * getContentType
	 * Renders the Content-Type header parameters specifically using binary/octet-stream,
	 * if we don't do this manually Apache will put text/html instead.
	 * 
	 * @return string
	 */
	protected function getContentType()
	{
		switch ($this->usageMode)
		{
			case self::USAGE_GET:
			case self::USAGE_HEAD:
				return 'Content-Type: binary/octet-stream';
			default:
				return '';
		}
	}
	
	/**
	 * getMetadata
	 * Render the metadata parameters obtained from the .meta file.
	 *
	 * @throws Exception
	 * 
	 * @return array
	 */
	protected function getMetadata()
	{
		$meta_array = array();
		
		if (isset($this->meta_file_content))
		{
			$meta = new \XPath($this->meta_file_content);
			
			foreach ($meta('/metadata/*', true) as $metaNode)
			{
				$meta_array[] = sprintf('%s%s: %s', Storage::META_HEADER_PREFIX, $metaNode->nodeName, $metaNode->nodeValue);
			}
		}
		
		return $meta_array;
	}
	
	/**
	 * Delete item (file and .meta file) from the disk.
	 *
	 * @throws StorageError
	 *
	 * @return none
	 */
	public function Delete()
	{
		$this->Lock(self::LOCK_MODE_RW);
	
		// Attempt to delete both files.  If either fails to delete, throw a StorageError.
		if (unlink($this->path_meta) === false)
		{
			LogError('Failed to delete path_meta', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}
	
		if (unlink($this->path) === false)
		{
			LogError('Failed to delete path', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}
	}
	
	/**
	 * Write stream to item's location on disk
	 * 
	 * @throws \Exception
	 * @throws HttpException
	 * 
	 * @return none
	 */
	public function Write()
	{
		if (!$this->stream)
			throw new \Exception('Stream not found');

		if ($this->lock_path != $this->_LockPath(self::LOCK_MODE_RW))
		{
			LogError('Not RW locked', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}

		$this->commited = false;

		$dir_path = dirname($this->path);
		if (!is_dir($dir_path))
			if (false === @mkdir($dir_path, 0755, true))
				throw new \Exception("Failed creating directory {$dir_path}");

		if (false === ($fh = @fopen($this->path, 'w')))
			throw new \Exception("Failed to open {$this->path}");

		stream_copy_to_stream($this->stream, $fh);
		if (false === @fclose($fh))
			throw new \Exception("Failed to close {$this->path}");

		$meta = new \DOMMaker('metadata');
		if (isset($this->metadata))
			foreach ($this->metadata as $meta_key => $meta_value)
				$meta->nodeCreate($meta_key, $meta_value);

		if (false === $meta->getDOMDocument()->save($this->path_meta))
			throw new \Exception("Failed to save metadata to file:{$this->path_meta}");

		unset($meta);

		$this->stream = null;
	}

	/**
	 * Commit it
	 *
	 * @throws StorageError
	 *
	 * @return none
	 */
	public function Commit()
	{
		if (isset($this->given_md5_base64) && $this->given_md5_base64 != base64_encode($this->md5 = md5_file($this->path, true)))
			throw new StorageError(StorageError::BadDigest, $this, Request::Get('id'));

		if ($this->final_path != $this->path)
		{
			if (!(rename($this->path, $this->final_path) && rename($this->path_meta, $this->final_path_meta)))
				$this->lc->LogCritical('Failed to rename to final path');

			$this->path = $this->final_path;
			$this->path_meta = $this->final_path_meta;
		}

		$this->commited = true;
	}

	/**
	 * Destructor
	 * Unlink's if not commited
	 */
	public function __destruct()
	{
		$this->Unlock();

		if (false === $this->commited)
		{
			$this->lc->LogWarning("Not committed; unlinked");

			if ($this->final_path != $this->path)
				unlink($this->path);

			if ($this->final_path_meta != $this->path_meta)
				unlink($this->path_meta);
		}
	}

	/**
	 * Lock path according to type
	 *
	 * @param string $mode see self::consts
	 *
	 * @return string
	 */
	private function _LockPath($mode)
	{
		$lock_base = config::get('storage.lock_base_path', '/tmp').DIRECTORY_SEPARATOR.$this->type.'_'.$this->org.'_'.$this->uuid.'.lock';
		return "{$lock_base}-{$mode}";
	}

	/**
	 * Is locked?
	 *
	 * @param string $mode see self::consts
	 *
	 * @throws \Exception
	 *
	 * @return bool
	 */
	private function _Locked($mode)
	{
		$lock_path = $this->_LockPath($mode);

		if (file_exists($lock_path))
		{
			if (filemtime($lock_path) >= (time() - self::LOCK_STALE_SECONDS))
			{
				$this->lc->LogWarning("Found active lock file:{$lock_path}");
				return true;
			}
			else
			{
				$this->lc->LogWarning("Unlinking inactive lock file:{$lock_path}");
				if (false === unlink($lock_path))
					throw new \Exception("Failed to remove lock file:{$lock_path}");
			}
		}

		return false;
	}

	/**
	 * Lock it!
	 *
	 * @param string $mode see self::consts
	 *
	 * @throws \Exception
	 * @throws HttpException
	 *
	 * @return none
	 */
	protected function Lock($mode)
	{
		if (!in_array($mode, array(self::LOCK_MODE_RO, self::LOCK_MODE_RW)))
			throw new \Exception('Unknown lock mode');

		if ($this->_Locked($mode) && $mode != self::LOCK_MODE_RO)
			throw new StorageError(StorageError::ObjectLocked, $this->id, Request::Get('id'));

		$lock_path = $this->_LockPath($mode);

		if (false === file_put_contents($lock_path, getmygid().'@'.gethostname()."\n", FILE_APPEND))
		{
			$this->lc->LogError("Failed creating lock file:{$lock_path}");
			throw new StorageError(StorageError::ObjectLocked, $this->id, Request::Get('id'));
		}

		$this->lock_path = $lock_path;
	}

	/**
	 * Unlock it
	 *
	 * @return none
	 */
	protected function Unlock()
	{
		if ($lock_path = $this->lock_path)
		{
			if (false === ($fh = @fopen($lock_path, 'r+')))
			{
				$this->lc->LogWarning("Unlock: Failed to open lock file:{$lock_path}");
				return;
			}

			if ('false' === config::get('storage.lock_disable_flock', 'false'))
				if (false === @flock($fh, LOCK_EX))
					$this->lc->LogWarning("Unlock: Failed to *flock* lock file:{$lock_path}; proceeding unlocked");

			$this_lock = getmygid().'@'.gethostname();
			$write_back = '';
			$unlocked = false;
			while ($line = @fgets($fh, 128))
			{
				$line = trim($line);

				if ($line == $this_lock)
				{
					$unlocked = true;
					continue;
				}

				$write_back .= "$line\n";
			}

			if (!$unlocked)
				$this->lc->LogWarning("Failed to unlock $this_lock in file:{$lock_path}");

			if ($write_back)
			{
				if (!(@rewind($fh) && @ftruncate($fh, strlen($write_back)) && @rewind($fh) && (strlen($write_back) === @fwrite($fh, $write_back))))
					$this->lc->LogWarning("Failed to write back lockers to file:$lock_path");
			}
			elseif (false === @unlink($lock_path))
				$this->lc->LogWarning("Failed to unlink lock file:$lock_path");

			if ('false' === config::get('storage.lock_disable_flock', 'false'))
				if (false === @flock($fh, LOCK_UN))
					$this->lc->LogWarning("Unlock: Failed to *flock(LOCK_UN)* lock file:{$lock_path}");

			fclose($fh);

			$this->lock_path = false;
		}
	}

	/**
	 * UuidToPath
	 * Generate path fragment according to config
	 *
	 * @param string $uuid Unique Identifier (Key) for the resource
	 *
	 * @return string
	 */
	protected static function UuidToPath($uuid)
	{
		$uuid_levels = min(config::get('storage.path_uuid_levels', 0), 10);
		$uuid = strtolower(str_replace('-', '', $uuid));

		$path = '';
		while ($uuid_levels)
		{
			$bit = substr($uuid, 0, 3);
			$uuid = substr($uuid, 3);
			$uuid_levels--;

			$path = sprintf('%s%s%s', $path, $path? DIRECTORY_SEPARATOR: '', $bit);
		}

		return sprintf('%s%s%s', $path, $path? DIRECTORY_SEPARATOR: '', $uuid);
	}
}

}
