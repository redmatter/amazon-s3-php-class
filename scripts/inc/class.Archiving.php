<?php

/**
 * Archiving
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
 * Constants and Utils
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Archiving
{
	const FILE_TYPE_UNKNOWN   = 0;
	const FILE_TYPE_CDR       = 1;
	const FILE_TYPE_RECORDING = 2;
	const FILE_TYPE_SMS       = 3;
	
	const SUBFORMAT_UNKNOWN   = 0; // flags errors
	const SUBFORMAT_BINARY    = 1; // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_XML       = 2;
	const SUBFORMAT_WAV       = 3;
	const SUBFORMAT_MP3       = 4;
	
	/**
	 * Get types in int form
	 * 
	 * @param string $str str
	 * 
	 * @return int
	 */
	public static function GetFileType($str)
	{
		switch (strtolower($str))
		{
			case 'cdr':
				return self::FILE_TYPE_CDR;
			case 'recording':
				return self::FILE_TYPE_RECORDING;
			case 'sms':
				return self::FILE_TYPE_SMS;
				
			default:
				return self::FILE_TYPE_UNKNOWN;
		}
	}
	
	public static function GetFileTypeStr($type)
	{
		switch ($type)
		{
			case self::FILE_TYPE_CDR:
				return 'cdr';
			case self::FILE_TYPE_RECORDING:
				return 'recording';
			case self::FILE_TYPE_SMS:
				return 'sms';
		
			default:
				return 'unknown';
		}
	}
	
	public static function GetSubFormat($str)
	{
		switch (strtolower($str))
		{
			case 'binary':
				return self::SUBFORMAT_BINARY;
			case 'xml':
				return self::SUBFORMAT_XML;
			case 'wav':
				return self::SUBFORMAT_WAV;
			case 'mp3':
				return self::SUBFORMAT_MP3;
				
			default:
				return self::SUBFORMAT_UNKNOWN;
		}
	}
	
	
	public static function GetSubFormatStr($subtype)
	{
		switch ($subtype)
		{
			case self::SUBFORMAT_BINARY:
				return 'binary';
			case self::SUBFORMAT_XML:
				return 'xml';
			case self::SUBFORMAT_WAV:
				return 'wav';
			case self::SUBFORMAT_MP3:
				return 'mp3';
	
			default:
				return 'unknown';
		}
	}
}