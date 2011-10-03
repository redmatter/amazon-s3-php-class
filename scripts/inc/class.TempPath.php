<?php

/**
 * Temp path ( file / directory )
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
 * Library
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */

class TempPath
{
	private $_path;
	private $_dir;
	private $_baseDir;
	private $_retain;

	/**
	 * Makes path to be used as temp file
	 * see http://php.net/manual/en/function.tempnam.php
	 * 
	 * @param string $prefix filename prefix ( filename OR sub-dir/filename ) 
	 * @param string $dir    dir to be used to create the file
	 * @param bool   $retain retain on destruction ( useful for debug )
	 */
	function __construct($prefix, $dir = null, $retain = false)
	{
		if (!isset($dir))
			$dir = config::get('app.TempDir', '/tmp');
			
		if (false === ($this->_baseDir = realpath($dir)))
			throw new Exception("base directory $dir doesnt exist OR execute permission missing");

		$dir = dirname($dir.'/'.$prefix);
		if (!is_dir($dir))
			if (true !== @mkdir($dir, 0700, true))
				throw new Exception("Unable to create path: $dir");

		$prefix = basename($prefix);

		$this->_dir = realpath($dir);
		$this->_path = tempnam($dir, $prefix);
		$this->_retain = $retain;

		LogVerbose($this->_path, 'TempPath::_path');
	}
	
	/**
	 * _cleanupDir
	 * 
	 * @param string $dir directory to delete
	 * 
	 * @return bool status of the operation; true on success
	 */
	private function _cleanupDir($dir = null)
	{
		if (is_null($dir))
			$dir = $this->_dir;

		if ($dir == $this->_baseDir)
			return;

		if (!is_dir($dir))
			return;

		$objects = @scandir($dir);
		$skipped = 0;
		foreach ($objects as $object)
		{
			if ($object != "." && $object != "..")
			{
				$object = $dir.'/'.$object;
				if (is_dir($object))
				{
					$this->_cleanupDir($object);
				}
				else
				{
					$stat = stat($object);
					if ($stat['mtime'] < (time() - 300 /*5min*/))
					{
						if (@unlink($object))
							LogDebug($object, 'Unlinked');
					}
					else
						$skipped++;
				}
			}
		}

		if (!$skipped)
			@rmdir($dir);
	}

	/**
	 * Set or reset retainable
	 * 
	 * @param bool $retain retain? true by default
	 * 
	 * @return none
	 */
	function setRetain($retain = true)
	{
		$this->_retain = $retain;
		LogDebug($retain, "{$this->_path}: retain");
	}

	/**
	 * Destructor; delete file if retain NOT set to true
	 */
	function __destruct()
	{
		if (!$this->_retain)
		{
			if (is_file($this->_path))
			{
				$ret = @unlink($this->_path);
				if (!$ret)
					LogWarning("{$this->_path}: Unlink failed");
				else
					LogVerbose("{$this->_path}: Unlinked");
			}
		}
		else
			LogVerbose("{$this->_path}: Retain=TRUE, Not unlinking file");
		
		$this->_cleanupDir();
	}

	/**
	 * Get path of the temp file
	 * 
	 * @return path
	 */
	function getPath()
	{
		return $this->_path;
	}

	/**
	 * Stringify this
	 * 
	 * @return path
	 */
	function __toString()
	{
		return $this->getPath();
	}
}
