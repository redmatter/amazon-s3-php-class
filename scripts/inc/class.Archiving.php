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
	const FILE_TYPE_UNKNOWN                = 0;
	const FILE_TYPE_CDR                    = 1;
	const FILE_TYPE_RECORDING              = 2;
	const FILE_TYPE_SMS                    = 3;
	const FILE_TYPE_BUFFERED_RECORDING     = 4;
	const FILE_TYPE_PCAP                   = 5;
	const FILE_TYPE_INFO                   = 6;
	const FILE_TYPE_PCAP_SIP_ONLY          = 7;

	const FILE_TYPE_STR_UNKNOWN            = 'unknown';
	const FILE_TYPE_STR_CDR                = 'cdr';
	const FILE_TYPE_STR_RECORDING          = 'recording';
	const FILE_TYPE_STR_SMS                = 'sms';
	const FILE_TYPE_STR_BUFFERED_RECORDING = 'buffered-recording';
	const FILE_TYPE_STR_PCAP               = 'pcap';
	const FILE_TYPE_STR_INFO               = 'info';
	const FILE_TYPE_STR_PCAP_SIP_ONLY      = 'pcap-sip-only';

	const FILE_TYPE_EXTENSION_UNKNOWN            = '.bin';
	const FILE_TYPE_EXTENSION_CDR                = '.cdr.xml';
	const FILE_TYPE_EXTENSION_RECORDING          = '.wav';
	const FILE_TYPE_EXTENSION_SMS                = '.xml';
	const FILE_TYPE_EXTENSION_BUFFERED_RECORDING = '.wav';
	const FILE_TYPE_EXTENSION_PCAP               = '.pcap';
	const FILE_TYPE_EXTENSION_INFO               = '.info.xml';
	const FILE_TYPE_EXTENSION_PCAP_SIP_ONLY      = '.sip.pcap';

	const SUBFORMAT_UNKNOWN                = 0; // flags errors
	const SUBFORMAT_BINARY                 = 1; // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_XML                    = 2;
	const SUBFORMAT_WAV                    = 3;
	const SUBFORMAT_MP3                    = 4;

	const SUBFORMAT_STR_UNKNOWN            = 'unknown'; // flags errors
	const SUBFORMAT_STR_BINARY             = 'binary';  // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_STR_XML                = 'xml';
	const SUBFORMAT_STR_WAV                = 'wav';
	const SUBFORMAT_STR_MP3                = 'mp3';

	static $OperationalDataFileTypes = array(
		self::FILE_TYPE_CDR,
		self::FILE_TYPE_PCAP,
		self::FILE_TYPE_PCAP_SIP_ONLY
	);

	static $OperationalDataFileTypesAsString = array(
		self::FILE_TYPE_CDR,
		self::FILE_TYPE_PCAP,
		self::FILE_TYPE_PCAP_SIP_ONLY
	);

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
			if (0 === strpos($name, 'FILE_TYPE_') && 0 !== strpos($name, 'FILE_TYPE_STR_') && $name != 'FILE_TYPE_UNKNOWN')
			{
				if ($includeSystemTypes || !in_array($value, self::$OperationalDataFileTypes))
					$ret[] = $value;
			}
		}

		return $ret;
	}

	/**
	 * Get all file-type strings
	 *
	 * @param bool $includeSystemTypes Include System Types
	 *
	 * @return array
	 */
	public static function GetAllFileTypesAsString($includeSystemTypes=true)
	{
		$ret = array();
		$refl = new ReflectionClass('Archiving');
		foreach ($refl->getConstants() as $name => $value)
		{
			if (0 === strpos($name, 'FILE_TYPE_STR_') && $name != 'FILE_TYPE_STR_UNKNOWN')
			{
				if ($includeSystemTypes || !in_array($value, self::$OperationalDataFileTypesAsString))
					$ret[] = $value;
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
		return in_array($type, self::$OperationalDataFileTypes);
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
			case self::FILE_TYPE_STR_CDR:
				return self::FILE_TYPE_CDR;
			case self::FILE_TYPE_STR_RECORDING:
				return self::FILE_TYPE_RECORDING;
			case self::FILE_TYPE_STR_SMS:
				return self::FILE_TYPE_SMS;
			case self::FILE_TYPE_STR_BUFFERED_RECORDING:
				return self::FILE_TYPE_BUFFERED_RECORDING;
			case self::FILE_TYPE_STR_PCAP:
				return self::FILE_TYPE_PCAP;
			case self::FILE_TYPE_STR_INFO:
				return self::FILE_TYPE_INFO;
			case self::FILE_TYPE_STR_PCAP_SIP_ONLY:
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
				return self::FILE_TYPE_STR_CDR;
			case self::FILE_TYPE_RECORDING:
				return self::FILE_TYPE_STR_RECORDING;
			case self::FILE_TYPE_SMS:
				return self::FILE_TYPE_STR_SMS;
			case self::FILE_TYPE_BUFFERED_RECORDING:
				return self::FILE_TYPE_STR_BUFFERED_RECORDING;
			case self::FILE_TYPE_PCAP:
				return self::FILE_TYPE_STR_PCAP;
			case self::FILE_TYPE_INFO:
				return self::FILE_TYPE_STR_INFO;
			case self::FILE_TYPE_PCAP_SIP_ONLY:
				return self::FILE_TYPE_STR_PCAP_SIP_ONLY;

			default:
				return 'unknown';
		}
	}

	/**
	 * File type to extension
	 *
	 * @param int $type The file type constant
	 *
	 * @return string
	 */
	public static function GetFileTypeExtension($type)
	{
		switch ($type)
		{
			case self::FILE_TYPE_CDR:
				return self::FILE_TYPE_EXTENSION_CDR;
			case self::FILE_TYPE_RECORDING:
				return self::FILE_TYPE_EXTENSION_RECORDING;
			case self::FILE_TYPE_SMS:
				return self::FILE_TYPE_EXTENSION_SMS;
			case self::FILE_TYPE_BUFFERED_RECORDING:
				return self::FILE_TYPE_EXTENSION_BUFFERED_RECORDING;
			case self::FILE_TYPE_PCAP:
				return self::FILE_TYPE_EXTENSION_PCAP;
			case self::FILE_TYPE_INFO:
				return self::FILE_TYPE_EXTENSION_INFO;
			case self::FILE_TYPE_PCAP_SIP_ONLY:
				return self::FILE_TYPE_EXTENSION_PCAP_SIP_ONLY;
			default:
				return self::FILE_TYPE_EXTENSION_UNKNOWN;
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
			case self::SUBFORMAT_STR_BINARY:
				return self::SUBFORMAT_BINARY;
			case self::SUBFORMAT_STR_XML:
				return self::SUBFORMAT_XML;
			case self::SUBFORMAT_STR_WAV:
				return self::SUBFORMAT_WAV;
			case self::SUBFORMAT_STR_MP3:
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
				return self::SUBFORMAT_STR_BINARY;
			case self::SUBFORMAT_XML:
				return self::SUBFORMAT_STR_XML;
			case self::SUBFORMAT_WAV:
				return self::SUBFORMAT_STR_WAV;
			case self::SUBFORMAT_MP3:
				return self::SUBFORMAT_STR_MP3;
			default:
				return self::SUBFORMAT_STR_UNKNOWN;
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
