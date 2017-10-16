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
	// the below pairs are only to keep legacy code happy
	// ** Do remember to maintain it! **
	const FILE_TYPE_INT_UNKNOWN            = 0;
	const FILE_TYPE_UNKNOWN                = self::FILE_TYPE_INT_UNKNOWN;
	const FILE_TYPE_INT_CDR                = 1;
	const FILE_TYPE_CDR                    = self::FILE_TYPE_INT_CDR;
	const FILE_TYPE_INT_RECORDING          = 2;
	const FILE_TYPE_RECORDING              = self::FILE_TYPE_INT_RECORDING;
	const FILE_TYPE_INT_SMS                = 3;
	const FILE_TYPE_SMS                    = self::FILE_TYPE_INT_SMS;
	const FILE_TYPE_INT_BUFFERED_RECORDING = 4;
	const FILE_TYPE_BUFFERED_RECORDING     = self::FILE_TYPE_INT_BUFFERED_RECORDING;
	const FILE_TYPE_INT_PCAP               = 5;
	const FILE_TYPE_PCAP                   = self::FILE_TYPE_INT_PCAP;
	const FILE_TYPE_INT_INFO               = 6;
	const FILE_TYPE_INFO                   = self::FILE_TYPE_INT_INFO;
	const FILE_TYPE_INT_PCAP_SIP_ONLY      = 7;
	const FILE_TYPE_PCAP_SIP_ONLY          = self::FILE_TYPE_INT_PCAP_SIP_ONLY;

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

	const FORMAT_CLEAR      = 0x00;
	const FORMAT_COMPRESSED = 0x01;
	const FORMAT_ENCRYPTED  = 0x10;

	const FORMAT_MASK_COMPRESSED = 0x0f;
	const FORMAT_MASK_ENCRYPTED  = 0xf0;

	const SUBFORMAT_UNKNOWN                = 0; // flags errors
	const SUBFORMAT_BINARY                 = 1; // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_XML                    = 2;
	const SUBFORMAT_WAV                    = 3;
	const SUBFORMAT_MP3                    = 4;
	const SUBFORMAT_PCAP                   = 5;

	const SUBFORMAT_STR_UNKNOWN            = 'unknown'; // flags errors
	const SUBFORMAT_STR_BINARY             = 'binary';  // BLOB ( catchall for any unrecognized data )
	const SUBFORMAT_STR_XML                = 'xml';
	const SUBFORMAT_STR_WAV                = 'wav';
	const SUBFORMAT_STR_MP3                = 'mp3';

	public static $OperationalDataFileTypes = array(
		self::FILE_TYPE_INT_CDR,
		self::FILE_TYPE_INT_PCAP,
		self::FILE_TYPE_INT_PCAP_SIP_ONLY
	);

	// data-types that an enduser does not get to access directly.
	// BUFFERED_RECORDING is a special kind and it does fall into this category
	public static $InternalDataFileTypes = array(
		self::FILE_TYPE_INT_CDR,
		self::FILE_TYPE_INT_PCAP,
		self::FILE_TYPE_INT_PCAP_SIP_ONLY,
		self::FILE_TYPE_INT_BUFFERED_RECORDING
	);

	public static $OperationalDataFileTypesAsString = array(
		self::FILE_TYPE_INT_CDR,
		self::FILE_TYPE_INT_PCAP,
		self::FILE_TYPE_INT_PCAP_SIP_ONLY
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
			if (0 === strpos($name, 'FILE_TYPE_INT_') && $name != 'FILE_TYPE_INT_UNKNOWN')
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
		$refl = new ReflectionClass(__CLASS__);
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
		return in_array($type, self::$OperationalDataFileTypes, true);
	}

	/**
	 * Is Operational data type?
	 *
	 * @param int $type one from the above file_type constants
	 *
	 * @return bool
	 */
	public static function FileTypeIsInternalData($type)
	{
		return in_array($type, self::$InternalDataFileTypes, true);
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
				return self::FILE_TYPE_INT_CDR;
			case self::FILE_TYPE_STR_RECORDING:
				return self::FILE_TYPE_INT_RECORDING;
			case self::FILE_TYPE_STR_SMS:
				return self::FILE_TYPE_INT_SMS;
			case self::FILE_TYPE_STR_BUFFERED_RECORDING:
				return self::FILE_TYPE_INT_BUFFERED_RECORDING;
			case self::FILE_TYPE_STR_PCAP:
				return self::FILE_TYPE_INT_PCAP;
			case self::FILE_TYPE_STR_INFO:
				return self::FILE_TYPE_INT_INFO;
			case self::FILE_TYPE_STR_PCAP_SIP_ONLY:
				return self::FILE_TYPE_INT_PCAP_SIP_ONLY;

			default:
				return self::FILE_TYPE_INT_UNKNOWN;
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
			case self::FILE_TYPE_INT_CDR:
				return self::FILE_TYPE_STR_CDR;
			case self::FILE_TYPE_INT_RECORDING:
				return self::FILE_TYPE_STR_RECORDING;
			case self::FILE_TYPE_INT_SMS:
				return self::FILE_TYPE_STR_SMS;
			case self::FILE_TYPE_INT_BUFFERED_RECORDING:
				return self::FILE_TYPE_STR_BUFFERED_RECORDING;
			case self::FILE_TYPE_INT_PCAP:
				return self::FILE_TYPE_STR_PCAP;
			case self::FILE_TYPE_INT_INFO:
				return self::FILE_TYPE_STR_INFO;
			case self::FILE_TYPE_INT_PCAP_SIP_ONLY:
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
			case self::FILE_TYPE_INT_CDR:
				return self::FILE_TYPE_EXTENSION_CDR;
			case self::FILE_TYPE_INT_RECORDING:
				return self::FILE_TYPE_EXTENSION_RECORDING;
			case self::FILE_TYPE_INT_SMS:
				return self::FILE_TYPE_EXTENSION_SMS;
			case self::FILE_TYPE_INT_BUFFERED_RECORDING:
				return self::FILE_TYPE_EXTENSION_BUFFERED_RECORDING;
			case self::FILE_TYPE_INT_PCAP:
				return self::FILE_TYPE_EXTENSION_PCAP;
			case self::FILE_TYPE_INT_INFO:
				return self::FILE_TYPE_EXTENSION_INFO;
			case self::FILE_TYPE_INT_PCAP_SIP_ONLY:
				return self::FILE_TYPE_EXTENSION_PCAP_SIP_ONLY;
			default:
				return self::FILE_TYPE_EXTENSION_UNKNOWN;
		}
	}

	/**
	 * Sub-format to int
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
            self::$_purge_states['finished_error']         = self::$_purge_states['finished'] | 0x02;
		}
	}

	/**
	 * State To string
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
	 * String to state
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

	/**
	 * Format SQL SELECT formula for binary UUID fields
	 *
	 * @param string $field field name
	 * @param string $alias field alias
	 *
	 * @return string
	 */
	public static function SqlSelectUuid($field, $alias = null)
	{
		return "LCASE(CONCAT(".
		"HEX(SUBSTR(`{$field}`, 1, 4)), '-', ".
		"HEX(SUBSTR(`{$field}`, 5, 2)), '-', ".
		"HEX(SUBSTR(`{$field}`, 7, 2)), '-', ".
		"HEX(SUBSTR(`{$field}`, 9, 2)), '-', ".
		"HEX(SUBSTR(`{$field}`, 11))" .
		"))".($alias !== null? " `{$alias}`": '');
	}

	/**
	 * Determine the object identifier based on the file type, Org ID and UUID of a record
	 *
	 * @param int    $file_type File type
	 * @param int    $org_id    Org ID
	 * @param string $uuid      UUID
	 *
	 * @return string
	 */
	public static function ConstructIdentifier($file_type, $org_id, $uuid)
	{
		return sprintf('%s/%d/%s', Archiving::GetFileTypeStr($file_type), $org_id, $uuid);
	}

	/**
	 * Get format integer; one used in Retention record
	 *
	 * @param bool $isCompressed compressed?
	 * @param bool $isEncrypted  encrypted?
	 * @param int  $subFormat    sub format
	 *
	 * @return int
	 */
	public static function GetFileFormat($isCompressed, $isEncrypted, $subFormat)
	{
		return ((($isCompressed ? self::FORMAT_COMPRESSED : 0) | ($isEncrypted ? self::FORMAT_ENCRYPTED : 0)) << 8) | $subFormat;
	}

	/**
	 * Extract the sub-format from format integer
	 *
	 * @param int $format format
	 *
	 * @return int
	 */
	public static function GetSubFormatFromFormat($format)
	{
		return $format & 0x00ff;
	}

	/**
	 * Is encrypted?
	 *
	 * @param int $format format
	 *
	 * @return bool
	 */
	public static function IsEncryptedFormat($format)
	{
		return 0 !== (($format >> 8) & self::FORMAT_MASK_ENCRYPTED);
	}

	/**
	 * Is compressed?
	 *
	 * @param int $format format
	 *
	 * @return bool
	 */
	public static function IsCompressedFormat($format)
	{
		return 0 !== (($format >> 8) & self::FORMAT_MASK_COMPRESSED);
	}
}
