<?php

/**
 * Archiving Retention
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
 * Controller
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class ArchivingRetention
{
	private $_dbc;
	private $_currentUser;

	public $UserID;
	public $EndpointID;
	public $Type;

	public $policy;

	/**
	 * ArchivingRetention - Retention lookup utility class
	 *
	 * @param object $dbc      database
	 * @param int    $org      org id
	 * @param int    $fileType fileType
	 * @param string $uuid_hex uuid as hex
	 *
	 * @return none
	 */
	private function __construct($dbc, $org, $fileType, $uuid_hex)
	{
		LogDebug($sql = "SELECT EndpointID FROM Retention_{$org} r WHERE Type={$fileType} AND UUID=0x{$uuid_hex} AND Purged='no'", 'SQL');
		$res = $dbc->query($sql);
		
		if ($row = $res->fetch_row())
		{
			$this->EndpointID = $row[0];
		}
		else
		{
			throw new Exception('Not Retention data found');
		}
	}

	/**
	 * Find
	 *
	 * @param object $dbc      database
	 * @param int    $org      org id
	 * @param int    $fileType fileType
	 * @param string $uuid_hex uuid as hex
	 *
	 * @return ArchivingRetention or false on error
	 */
	public static function Find($dbc, $org, $fileType, $uuid_hex)
	{
		return new ArchivingRetention($dbc, $org, $fileType, $uuid_hex);
	}
}

