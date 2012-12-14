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
namespace rmarchgateway\s3entities
{
	
use rmarchgateway\s3entities\S3Bucket;

use rmarchgateway\renderables\StorageError;

use fluid\Request;

use \config;

/**
 * Storage Item
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class S3Object
{
	const LOCK_STALE_SECONDS = 3600;
	const LOCK_MODE_RW = 'rw';
	const LOCK_MODE_RO = 'ro';

	protected $committed = null;
	
	protected $stream;
	
	public $md5 = null;
	
	public $id;
	
	protected $bucket;
	protected $key;
	
	protected $path;
	protected $path_meta;
	protected $lock_path = false;
	
	protected $final_path;
	protected $final_path_meta;
	
	protected $stat_path = null;
	protected $stat_meta = null;

	protected $lc;

	/**
	 * Constructor
	 * 
	 * @param string   $bucket S3 Bucket
	 * @param string   $key    Key
	 *
	 * @throws StorageError
	 * @throws Exception
	 */
	public function __construct($bucket, $key)
	{
		// Initialise all member variables from constructor parameters.
		$this->bucket = new S3Bucket($bucket);
		$this->key = $key;

		$this->path = $this->bucket->getPath() . DIRECTORY_SEPARATOR . self::UuidToPath($key);
		$this->path_meta = $this->path.'.meta';

		$this->final_path = $this->path;
		$this->final_path_meta = $this->path_meta;

		if (is_file($this->path))
		{
			$this->stat_path = @stat($this->path);
		}

		if (is_file($this->path_meta))
		{
			$this->stat_meta = @stat($this->path_meta);
		}

		$this->id = "/{$bucket}/{$this->key}";

		$this->lc = new \SysLogContext($this->id);
	}

	/**
	 * getObjectExists
	 * Returns true if the path held points to an extant file.
	 *
	 * @return bool
	 */
	public function getObjectExists()
	{
		return is_file($this->path);
	}

	/**
	 * getMetaExists
	 * Returns true if the metadata path held points to an extant file.
	 *
	 * @return bool
	 */
	public function getMetaExists()
	{
		return is_file($this->path_meta);
	}
	
	/**
	 * getFileStream
	 * Returns a file handle to the path held.
	 * Returns null if this fails.
	 * 
	 * @throws StorageError
	 * @throws Exception
	 *
	 * @return resource
	 */
	public function getFileStream()
	{
		$this->Lock(self::LOCK_MODE_RO);
		
		if ((isset($this->stat_path) === false) || ($this->stat_path === false))
		{
			LogWarning("File does not exist: $this->path");
			return null;
		}
		
		$this->stream = @fopen($this->path, 'rb');

		if ($this->stream === false)
		{
			LogWarning("Failed to open file stream: $this->path");
			return null;
		}
		else
		{
			return $this->stream;
		}
	}

	/**
	 * getMetadata
	 * Returns an array containing each metadata element from the metadata file, if there is a file.
	 * Returns null if this fails.
	 *
	 * @throws Exception
	 *
	 * @return array
	 */
	public function getMetadata()
	{
		$meta_array = array();
		if ((isset($this->stat_meta) === true) && ($this->stat_meta !== false))
		{
			$meta_content = @file_get_contents($this->path_meta);
			if (isset($meta_content))
			{
				$meta = new \XPath($meta_content);
				foreach ($meta('/metadata/*', true) as $metaNode)
				{
					$meta_array[$metaNode->nodeName] = $metaNode->nodeValue;
				}
			}
		}

		return $meta_array;
	}
	
	/**
	 * getMetadata
	 * Returns an MD5 of the file.
	 * Only does this once, if the MD5 has previously been calculated, it returns this value.
	 *
	 * @return string
	 */
	public function getMd5()
	{
		// If we already have an MD5 set, just return it.
		if (isset($this->md5))
		{
			return $this->md5;
		}
		// Otherwise, calculate it and store it for quicker retrieval later.
		else
		{
			$this->md5 = @md5_file($this->path, true);
			return $this->md5;
		}
	}
	
	/**
	 * getMetadata
	 * Returns the date/time that the file at the path held was last modified.
	 * Returns null if this value can't be found (probably because the file doesn't exist).
	 *
	 * @return DateTime
	 */
	public function getLastModified()
	{
		if (isset($this->stat_path['mtime']))
		{
			return new \DateTime('@'.$this->stat_path['mtime'], new \DateTimeZone('UTC'));
		}
		else
		{
			return null;
		}
	}
	
	/**
	 * getFileLength
	 * Returns the file length of the file at the path held.
	 * Returns null if this value can't be found (probably because the file doesn't exist).
	 *
	 * @return int
	 */
	public function getFileLength()
	{
		if ($this->stat_path === false)
		{
			LogWarning('$this->stat_path is false', $this->id);
			return null;
		}
		
		if (isset($this->stat_path['size']) === false)
		{
			LogError('$this->stat_path[\'size\'] is not set', $this->id);
			return null;
		}
		
		return $this->stat_path['size'];
	}

	/**
	 * Write
	 * Writes stream to object's location on disk defined by path and
	 * metadata to the location on disk defined by meta_path.
	 *
	 * @throws \Exception
	 *
	 * @return none
	 */
	public function Write($stream, $metadata)
	{
		if (isset($stream) === false)
		{
			throw new \Exception('Stream is null');
		}
		
		$path_exists = is_file($this->path);
		$path_meta_exists = is_file($this->path_meta);
		
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
	
		if ($this->lock_path != $this->_LockPath(self::LOCK_MODE_RW))
		{
			LogError('Not RW locked', $this->id);
			throw new \Exception('Not RW locked');
		}
	
		$this->committed = false;
	
		$dir_path = dirname($this->path);
		if (!is_dir($dir_path))
		{
			if (false === @mkdir($dir_path, 0755, true))
			throw new \Exception("Failed creating directory {$dir_path}");
		}
	
		if (false === ($fh = @fopen($this->path, 'w')))
			throw new \Exception("Failed to open {$this->path}");
	
		stream_copy_to_stream($stream, $fh);
		if (false === @fclose($fh))
			throw new \Exception("Failed to close {$this->path}");
	
		$meta = new \DOMMaker('metadata');
		if (isset($metadata))
			foreach ($metadata as $meta_key => $meta_value)
			$meta->nodeCreate($meta_key, $meta_value);
	
		if (false === $meta->getDOMDocument()->save($this->path_meta))
			throw new \Exception("Failed to save metadata to file:{$this->path_meta}");
	
		unset($meta);
	}

	/**
	 * Commit
	 * Commit it
	 *
	 * @throws StorageError
	 * @throws Exception
	 *
	 * @return none
	 */
	public function Commit($md5_base_64 = null)
	{
		if (isset($md5_base_64))
		{
			$file_md5 = base64_encode($this->getMd5());
			if ($md5_base_64 != $file_md5)
			{
				LogError("MD5 hash failed, md5_base_64: print_r($md5_base_64, true), this->getMd5: print_r($file_md5, true)");
				throw new StorageError(StorageError::BadDigest, $this, Request::Get('id'));
			}
		}

		if ($this->final_path != $this->path)
		{
			if (!(rename($this->path, $this->final_path) && rename($this->path_meta, $this->final_path_meta)))
			{
				$this->lc->LogCritical('Failed to rename to final path');
				throw new \Exception('Failed to rename to final path');
			}

			$this->path = $this->final_path;
			$this->path_meta = $this->final_path_meta;
		}

		$this->committed = true;
	}

	/**
	 * Delete
	 * Delete item (file and .meta file) from the disk.
	 *
	 * @throws Exception
	 *
	 * @return none
	 */
	public function Delete()
	{
		$this->Lock(self::LOCK_MODE_RW);

		// Only attempt to delete the meta file if there is one present.
		if (isset($this->stat_meta))
		{
			if (unlink($this->path_meta) === false)
			{
				LogError('Failed to delete path_meta', $this->id);
				throw new \Exception('Failed to delete path_meta');
			}	
		}

		// Always try to delete the main object file.  If it's not present, or fails to delete, throw an error.
		if (unlink($this->path) === false)
		{
			LogError('Failed to delete path', $this->id);
			throw new \Exception('Failed to delete path');
		}
	}
	
	/**
	 * Lock
	 * Lock it!
	 *
	 * @param string $mode see self::consts
	 *
	 * @throws \Exception
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
	 * _LockPath
	 * Lock path
	 *
	 * @param string $mode see self::consts
	 *
	 * @return string
	 */
	private function _LockPath($mode)
	{
		$path = $this->bucket->getName() . '_' . str_replace('/','_', $this->key);
		$lock_base = config::get('storage.lock_base_path', '/tmp').DIRECTORY_SEPARATOR.$path.'.lock';
		return "{$lock_base}-{$mode}";
	}

	/**
	 * _Locked
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
	 * UuidToPath
	 * Generate path fragment according to config.
	 * This allows the archive gateway to ensure that a configurable maximum number of entities exist
	 * within each directory, regardless of the path the client passes.
	 *
	 * @param string $uuid Unique Identifier (Key) for the resource
	 *
	 * @return string
	 */
	protected static function UuidToPath($uuid)
	{
		// Perform a SHA1 on the UUID,  remove the hyphens and force to lower case so that, regardless of
		// what's passed by the client, we can guarantee the UUID is made up of 40 lower case hexadecimal characters. 
		$uuid = strtolower(str_replace('-', '', sha1($uuid)));

		// Determine the depth of directory nesting which has been configured.
		$uuid_levels = min(config::get('storage.path_uuid_levels', 0), 10);

		$path = '';
		while ($uuid_levels)
		{
			// Take three characters at a time to create the directory names.
			// As the characters are hexadecimal, this provides 4096 (16^3) possibilities,
			// which is a reasonable maximum number of folders for a file system to cope with.
			$bit = substr($uuid, 0, 3);
			$uuid = substr($uuid, 3);
			$uuid_levels--;

			$path .= ($bit . DIRECTORY_SEPARATOR);
		}

		return ($path . $uuid);
	}
	
	/**
	 * Destructor
	 * Ensure any locks are removed and resources freed.
	 */
	public function __destruct()
	{
		$this->Unlock();
		
		if (false === $this->committed)
		{
			$this->lc->LogWarning("Not committed; unlinked");
		
			if ($this->final_path != $this->path)
				unlink($this->path);
		
			if ($this->final_path_meta != $this->path_meta)
				unlink($this->path_meta);
		}
		
		if (isset($this->stream) === true)
		{
			@fclose($this->stream);
			$this->stream = null;
		}
	}
}

}
