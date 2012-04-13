<?php

namespace rmarchgateway
{

use rmarchgateway\resources\Storage;

use rmarchgateway\renderables\StorageError;

use fluid\Request;

use fluid\HttpException;

use fluid\Http404NotFound;

use \config;

class StorageItem
{
	const LOCK_STALE_SECONDS = 3600;
	const LOCK_MODE_RW = 'rw';
	const LOCK_MODE_RO = 'ro';
	
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
	protected $persist = false;
	protected $stream = null;
	protected $commited = null;
	protected $lock_path = false;
	
	protected $lc;
	
	public function __construct($bucket, $org, $type, $uuid, $metadata=null, $stream=null, $md5_base64=null)
	{
		$this->org = $org;
		$this->type = $type;
		$this->uuid = $uuid;
		
		$this->id = "/{$bucket}/{$type}/{$org}/{$uuid}";
		
		$this->lc = new \SysLogContext($this->id);
		
		
		$this->final_path = $this->path = config::get('storage.base_path', '/tmp').DIRECTORY_SEPARATOR.$org.DIRECTORY_SEPARATOR.$type.DIRECTORY_SEPARATOR.self::UuidToPath($uuid);
		$this->final_path_meta = $this->path_meta = $this->path.'.meta';
		
		$this->stream = $stream;
		
		if (!is_null($stream))
		{
			if (is_null($md5_base64))
				throw new HttpException(400, 'No MD5 given');

			if (is_null($metadata) || (is_array($metadata) && !count($metadata)))
				throw new HttpException(400, 'No Metadata');
				
			$this->metadata = $metadata;
				
			$this->given_md5_base64 = $md5_base64;
			
			// check if the file already exists
			if (is_file($this->path) && is_file($this->path_meta))
			{
				$tmp = '.'.time().getmypid();
				$this->path .= $tmp;
				$this->path_meta .= $tmp;
			}
			
			$this->Lock(self::LOCK_MODE_RW);
		}
		else
		{
			$this->Lock(self::LOCK_MODE_RO);
		}
	}
	
	public function Render($requestId, $withBody=false, $withMetadata=false)
	{
		if ($this->lock_path != $this->_LockPath(self::LOCK_MODE_RO))
			throw new HttpException(500, 'Not RO locked');
		
		$meta = new \XPath(file_get_contents($this->path_meta));
		
		$stat = stat($this->path);
		$lastModTime = new \DateTime('@'.$stat['mtime'], new \DateTimeZone('UTC'));
		
		if (!isset($this->md5))
			$this->md5 = md5_file($this->path, true);
		
		LogMessage(sprintf('Last-Modified: %s', $lastModTime->format(\DateTime::COOKIE)));
		
		header("Request-Id: $requestId");
		header('Server: RedMatter');
		header(sprintf('ETag: "%s"', bin2hex($this->md5)));
		header(sprintf('Last-Modified: %s', $lastModTime->format(\DateTime::COOKIE)));
		if ($withMetadata)
			foreach ($meta('/metadata/*', true) as $metaNode)
				header(sprintf('%s%s: %s', Storage::META_HEADER_PREFIX, $metaNode->nodeName, $metaNode->nodeValue));

		header('Content-Type: binary/octet-stream');
		header(sprintf('Content-Length: %d', filesize($this->path)));
		if ($withBody)
		{
			if (false === ($this->stream = @fopen($this->path, 'rb')))
				throw new Exception('Failed opening stream');
			
			$out = fopen('php://output', 'wb');
			stream_copy_to_stream($this->stream, $out);
			fclose($out);
			fclose($this->stream);
		}
	}
	
	public function Write()
	{
		if (!$this->stream)
			throw new \Exception('Stream not found');
		
		if ($this->lock_path != $this->_LockPath(self::LOCK_MODE_RW))
			throw new HttpException(500, 'Not RW locked');
		
		$this->commited = false;
		
		$dir_path = dirname($this->path);
		if (!is_dir($dir_path))
			if (false === mkdir($dir_path, 0755, true))
				throw new \Exception("Failed creating directory {$dir_path}");
		
		if (false === ($fh = @fopen($this->path, 'w')))
			throw new \Exception("Failed to open {$this->path}");
		
		stream_copy_to_stream($this->stream, $fh);
		if (false === fclose($fh))
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
	
	private function _LockPath($mode)
	{
		$lock_base = config::get('storage.lock_base_path', '/tmp').DIRECTORY_SEPARATOR.$this->type.'_'.$this->org.'_'.$this->uuid.'.lock';
		return "{$lock_base}-{$mode}";
	}
	
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
	
	protected function Lock($mode)
	{
		if (!in_array($mode, array(self::LOCK_MODE_RO, self::LOCK_MODE_RW)))
			throw new \Exception('Unknown lock mode');

		if ($this->_Locked($mode) && $mode != self::LOCK_MODE_RO)
			throw new HttpException(423);

		$lock_path = $this->_LockPath($mode);
				
		if (false === file_put_contents($lock_path, getmygid().'@'.gethostname()."\n", FILE_APPEND))
		{
			$this->lc->LogError("Failed creating lock file:{$lock_path}");
			throw new HttpException(423);
		}
		
		$this->lock_path = $lock_path;
	}
	
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
