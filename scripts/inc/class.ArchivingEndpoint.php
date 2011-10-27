<?php

/**
 * Controller using the REST architecture
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
class ArchivingEndpoint
{
	public $id;
	public $org;
	public $encrypted;
	public $compressed;
	public $retentionManaged;
	public $type;
	public $accessKey;
	public $secretKey;
	public $encryptionKey;
	public $bucket;

	private static $_objs = array();

	/**
	 * Cached getter of instance
	 *
	 * @param dbc $dbc dbc
	 * @param int $org org
	 * @param int $id  id
	 *
	 * @return ArchivingEndpoint
	 */
	public static function Get($dbc, $org, $id)
	{
		if (!isset(self::$_objs[$id]))
			self::$_objs[$id] = array();

		if (!isset(self::$_objs[$id][$org]))
			self::$_objs[$id][$org] = new ArchivingEndpoint($dbc, $org, $id);

		return self::$_objs[$id][$org];
	}

	/**
	 * updateFromDb
	 *
	 * @return bool
	 */
	function updateFromDb()
	{
		$res = $this->_dbc->query("SELECT EncryptionKey FROM Archiving.Provisioning WHERE OrgID={$this->org}");
		if (!($row = $res->fetch_object()))
			throw new Exception('Not provisioned yet');

		$this->encryptionKey = $row->EncryptionKey;

		LogDebug($sql = "SELECT Type, Encrypted, Compressed, RetentionManaged FROM Archiving.StorageEndpoint WHERE ID={$this->id}", 'SQL');
		$res = $this->_dbc->query($sql);
		if (!($row = $res->fetch_object()))
			throw new Exception('Endpoint not found (2)');

		$this->type = $row->Type;
		$this->encrypted = $row->Encrypted;
		$this->compressed = $row->Compressed;
		$this->retentionManaged = $row->RetentionManaged;

		$details_loaded = false;
		switch($this->type)
		{
		case 's3':
			$res = $this->_dbc->query("SELECT AccessKey, SecretKey, Bucket, Region FROM Archiving.StorageEndpointS3 WHERE ID={$this->id}");
			if ($row = $res->fetch_object())
			{
				$this->accessKey = $row->AccessKey;
				$this->secretKey = $row->SecretKey;
				$this->bucket = $row->Bucket;
				$this->region = $row->Region;

				$details_loaded = true;
			}
			break;

		case 'file-system':
			$res = $this->_dbc->query("SELECT BasePath, SlowMedium FROM Archiving.StorageEndpointFileSystem WHERE ID={$this->id}");
			if ($row = $res->fetch_object())
			{
				$this->basePath = $row->BasePath;
				$this->slowMedium = $row->SlowMedium;

				$details_loaded = true;
			}
			break;

		case 's3-emulation':
			$res = $this->_dbc->query("SELECT AccessKey, SecretKey, Bucket, BaseURI FROM Archiving.StorageEndpointS3Emulation WHERE ID={$this->id}");
			if ($row = $res->fetch_object())
			{
				$this->accessKey = $row->AccessKey;
				$this->secretKey = $row->SecretKey;
				$this->bucket = $row->Bucket;
				$this->baseURI = $row->BaseURI;

				$details_loaded = true;
			}
			break;
		}

		if (false === $details_loaded)
			throw new Exception('Endpoint Details missing; Internal error');

		return true;
	}

	/**
	 * Constructor
	 *
	 * @param dbc $dbc        dbc
	 * @param int $rmOrgId    org
	 * @param int $endpointID id
	 *
	 * @throws Exception
	 */
	function __construct($dbc, $rmOrgId, $endpointID)
	{
		$this->_dbc = $dbc;
		$this->org = $rmOrgId;
		$this->id = $endpointID;

		if (false === $this->updateFromDb())
			throw new Exception('Storage Endpoint not found');
	}
}
