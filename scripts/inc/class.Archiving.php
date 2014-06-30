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
	const FILE_TYPE_UNKNOWN            = 0;
	const FILE_TYPE_CDR                = 1;
	const FILE_TYPE_RECORDING          = 2;
	const FILE_TYPE_SMS                = 3;
	const FILE_TYPE_BUFFERED_RECORDING = 4;
	const FILE_TYPE_PCAP               = 5;
	const FILE_TYPE_INFO               = 6;
	const FILE_TYPE_PCAP_SIP_ONLY      = 7;

	const SUBFORMAT_UNKNOWN   = 0; // flags errors
	const SUBFORMAT_BINARY    = 1; // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_XML       = 2;
	const SUBFORMAT_WAV       = 3;
	const SUBFORMAT_MP3       = 4;

	const CONFIG_FILE_RMARCHIVED = '/etc/archiving/rmarchived.conf';

	/**
	 * Get all defined file-types
	 *
	 * @param bool $includeSystemTypes Include System Types
	 *
	 * @return array
	 */
	public static function GetAllFileTypes($includeSystemTypes=true)
	{
		$ret = array();
		$refl = new ReflectionClass('Archiving');
		foreach ($refl->getConstants() as $name => $value)
		{
			if (0 === strpos($name, 'FILE_TYPE_') && $name != 'FILE_TYPE_UNKNOWN')
			{
				if (
					$includeSystemTypes ||
					in_array(
						$value,
						array(
							self::FILE_TYPE_RECORDING,
							self::FILE_TYPE_BUFFERED_RECORDING,
							self::FILE_TYPE_INFO,
							self::FILE_TYPE_PCAP,
							self::FILE_TYPE_PCAP_SIP_ONLY,
							self::FILE_TYPE_SMS
						)
					)
				)
				{
					$ret[] = $value;
				}
			}
		}

		return $ret;
	}

	/**
	 * Is Operational data type?
	 *
	 * @param int $type one from the above file_type constants
	 *
	 * @return bool
	 */
	public static function FileTypeIsOperationalData($type)
	{
		return in_array($type, array(self::FILE_TYPE_CDR, self::FILE_TYPE_PCAP, self::FILE_TYPE_PCAP_SIP_ONLY));
	}

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
			case 'buffered-recording':
				return self::FILE_TYPE_BUFFERED_RECORDING;
			case 'pcap':
				return self::FILE_TYPE_PCAP;
			case 'info':
				return self::FILE_TYPE_INFO;
			case 'pcap-sip-only':
				return self::FILE_TYPE_PCAP_SIP_ONLY;

			default:
				return self::FILE_TYPE_UNKNOWN;
		}
	}

	/**
	 * File-type to string
	 *
	 * @param int $type file-type
	 *
	 * @return string
	 */
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
			case self::FILE_TYPE_BUFFERED_RECORDING:
				return 'buffered-recording';
			case self::FILE_TYPE_PCAP:
				return 'pcap';
			case self::FILE_TYPE_INFO:
				return 'info';
			case self::FILE_TYPE_PCAP_SIP_ONLY:
				return 'pcap-sip-only';

			default:
				return 'unknown';
		}
	}

	/**
	 * sub-format to int
	 *
	 * @param string $str subformat string
	 *
	 * @return int
	 */
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

	/**
	 * Subformat from string
	 *
	 * @param int $subtype sub-type
	 *
	 * @return string
	 */
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

	private static $_purge_states = null;

	/**
	 * Setup the states array
	 *
	 * @return void
	 */
	private static function _PopulateStates()
	{
		if (is_null(self::$_purge_states))
		{
			self::$_purge_states = array();

			self::$_purge_states['invalid']                = 0x00;

			self::$_purge_states['init']                   = 0x01 << 4;
			self::$_purge_states['init_ok']                = self::$_purge_states['init'] | 0x01;
			self::$_purge_states['init_error']             = self::$_purge_states['init'] | 0x02;

			self::$_purge_states['approve']                = 0x02 << 4;
			self::$_purge_states['approve_ok']             = self::$_purge_states['approve'] | 0x01;
			self::$_purge_states['approve_error']          = self::$_purge_states['approve'] | 0x02;

			self::$_purge_states['delete_file']            = 0x03 << 4;
			self::$_purge_states['delete_file_ok']         = self::$_purge_states['delete_file'] | 0x01;
			self::$_purge_states['delete_file_error']      = self::$_purge_states['delete_file'] | 0x02;

			self::$_purge_states['delete_usage']           = 0x04 << 4;
			self::$_purge_states['delete_usage_ok']        = self::$_purge_states['delete_usage'] | 0x01;
			self::$_purge_states['delete_usage_error']     = self::$_purge_states['delete_usage'] | 0x02;

			self::$_purge_states['delete_retention']       = 0x05 << 4;
			self::$_purge_states['delete_retention_ok']    = self::$_purge_states['delete_retention'] | 0x01;
			self::$_purge_states['delete_retention_error'] = self::$_purge_states['delete_retention'] | 0x02;

			self::$_purge_states['finished']               = 0x0f << 4;
		}
	}

	/**
	 * state To string
	 *
	 * @param int $state state int value
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	public static function PurgeStateToString($state)
	{
		if (is_null(self::$_purge_states))
			self::_PopulateStates();

		$state_nv = array_filter(
			self::$_purge_states,
			function($v) use($state)
			{
				return $v == $state;
			}
		);

		if (count($state_nv) != 1)
			throw new Exception("Unknown purge-state: $state");

		return array_shift(array_keys($state_nv));
	}

	/**
	 * string to state
	 *
	 * @param string $str state as string
	 *
	 * @throws Exception
	 *
	 * @return bool
	 */
	public static function PurgeStateFromString($str)
	{
		if (is_null(self::$_purge_states))
			self::_PopulateStates();

		if (isset(self::$_purge_states[$str]))
			return self::$_purge_states[$str];

		throw new Exception("Unknown purge-state: $str");
	}
}
