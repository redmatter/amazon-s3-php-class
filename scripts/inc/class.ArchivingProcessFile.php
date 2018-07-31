<?php
/**
 * ArchivingSmsRecovery
 *
 * PHP version 5
 *
 * @category Library
 * @package  RM-archiving
 * @author   Andrew Bulford <andrew.bulford@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 */

/**
 * Class ExtractFile
 *
 * PHP version 5
 *
 * @category Library
 * @package  RM-archiving-scripts
 * @author   Original: Andrew Bulford <andrew.bulford@redmatter.com>
 * @author   Latest: $Author: $ <dev@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @link     http://trac.redmatter.com/
 */
class ArchivingProcessFile
{
	const ARCHIVING_FORMAT_RMA_CE = 'RMA-CE';
	const ARCHIVING_FORMAT_RMA = 'RMA';

	/**
	 * ProcessFile
	 *
	 * @param string $source_file_path Path to the file being extracted
	 * @param string $dest_file_path   Path where the output should be put
	 * @param string $encryption_key   Encryption key to use for decryption, if required
	 *
	 * @throws Exception
	 *
	 * @return void
	 */
	public static function ProcessFile($source_file_path, $dest_file_path, $encryption_key = null)
	{
		// Check the inputs.
		$dest_directory = basename($dest_file_path);
		if (!file_exists($source_file_path))
			throw new Exception("Source file path {$source_file_path} does not exist");
		if (!is_writable($dest_file_path))
			throw new Exception("Destination directory {$dest_directory} is not writable");

		$pid = pcntl_fork();
		if ($pid == -1)
		{
			throw new Exception('Could not fork');
		}

		if ($pid)
		{
            $status = null;

			// sleep and wait for the convert to finish; also need to keep an eye on any service-stop request
			while (0 === pcntl_waitpid($pid, $status, WNOHANG))
			{
				if (DaemonScripts::Terminate())
				{
					posix_kill($pid, SIGABRT);
				}

				usleep(100000);
			}

			if (pcntl_wifexited($status))
			{
				$status = pcntl_wexitstatus($status);
				if ($status)
				{
					throw new Exception("Failed to convert {$source_file_path}");
				}

				LogVerbose("Decrypted $source_file_path");
			}
			elseif (pcntl_wifsignaled($status))
			{
				$signal = pcntl_wtermsig($status);
				throw new Exception("Failed to convert {$source_file_path}; killed by signal {$signal}");
			}
			elseif (pcntl_wifstopped($status))
			{
				$signal = pcntl_wstopsig($status);
				throw new Exception("Failed to convert {$source_file_path}; stopped by signal {$signal}");
			}
		}
		else
		{
			pcntl_exec('/usr/bin/archiving-convert', array(
				'--source', $source_file_path,
				'--target', $dest_file_path,
				'--key', base64_encode($encryption_key),
				'--log.syslog-tag', APP_NAME_.'-convert',
			));
		}
	}

	/**
	 * ProcessFileContent
	 *
	 * @param string $content        Content to be processed
	 * @param string $encryption_key Encryption key to use for decryption, if required
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	public static function ProcessFileContent($content, $encryption_key = null)
	{
		$content_length = strlen($content);
		if ($content_length < 3)
			throw new Exception("Content length ({$content_length}) is definitely too short");

		// File pointer
		$fp = 0;

		if (substr($content, $fp, 6) === self::ARCHIVING_FORMAT_RMA_CE)
		{
			$fp += 6;

			LogMessage("Format ".self::ARCHIVING_FORMAT_RMA_CE." indicates that content is encrypted, decrypting");

			if ($content_length < $fp + 5 + 8)
				throw new Exception("Content length is too short for the ".self::ARCHIVING_FORMAT_RMA_CE." format");

			if (empty($encryption_key))
			{
				throw new Exception(
					"Format indicates that content is encrypted, but no encryption key was specified"
				);
			}

			$unpack_format = 'CArchiveDVersionMajor/'.
				'CArchiveDVersionMinor/'.
				'CArchiveDVersionPatch/'.
				'CFormatVersionMajor/'.
				'CFormatVersionMinor/'.
				'a8InitialisationVector';

			$header_components = unpack($unpack_format, substr($content, $fp, 13));
			$fp += 13;

			$archived_version_major = $header_components['ArchiveDVersionMajor'];
			$archived_version_minor = $header_components['ArchiveDVersionMinor'];
			$archived_version_patch = $header_components['ArchiveDVersionPatch'];
			$format_version_major = $header_components['FormatVersionMajor'];
			$format_version_minor = $header_components['FormatVersionMinor'];
			LogVerbose(
				"ArchiveD version {$archived_version_major}.{$archived_version_minor}.{$archived_version_patch}, ".
				"Format version {$format_version_major}.{$format_version_minor}"
			);
			
			$iv = $header_components['InitialisationVector'];
			
			// PLAT-4214 PLAT-4172
			// unpack has a bug where it wrongly trims off any trailing NUL bytes from unpacked data 
			// (even though we specify a format of a8 for the iv - NUL-padded string 8 bytes long)
			// See https://bugs.php.net/bug.php?id=61038
			//
			// This bug was fixed in PHP v5.5 but workaround for this (older) version of PHP is to just re-append the NULs again 
			$pad = 8 - strlen($iv);
			if ($pad > 0) 
			{
				for ($i=0; $i<$pad; ++$i)
					$iv .= "\0";
			}
			
			// Jump past the padding.
			$fp += 21;

			$content = mcrypt_decrypt(
				MCRYPT_BLOWFISH,
				$encryption_key,
				substr($content, $fp),
				MCRYPT_MODE_CBC,
				$iv
			);

			$fp = 0;
		}

		if (substr($content, $fp, 3) !== self::ARCHIVING_FORMAT_RMA)
			throw new Exception("Unrecognised file format found");

		$fp += 3;

		$unpack_format = 'CFormat/'.
			'CSubFormat/'.
			'L2ContentLength/'.
			'L2InflatedSize/'.
			'LCrc32';

		$header_components = unpack($unpack_format, substr($content, $fp, 22));

		// Push file pointer past header we just read in and 7 byte padding
		$fp += (22 + 7);

		$content_length = ($header_components['ContentLength2'] << 32) + $header_components['ContentLength1'];
		$inflated_size = ($header_components['InflatedSize2'] << 32) + $header_components['InflatedSize1'];

		LogVerbose(
			"Format: {$header_components['Format']}".
			", Sub-format: {$header_components['SubFormat']}".
			", Content length: {$content_length}".
			", Inflated size: {$inflated_size}".
			", CRC32: {$header_components['Crc32']}"
		);

		// Decompress content
		$content = bzdecompress(substr($content, $fp));

		// Check CRC
		$actual_crc32 = crc32($content);
		if ($actual_crc32 != $header_components['Crc32'])
			throw new Exception("CRC32 failed: {$actual_crc32} != {$header_components['Crc32']}");

		return $content;
	}
}
