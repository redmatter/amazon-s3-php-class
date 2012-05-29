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
	public static function Get($dbc, $org, $id, $ignoreEncKey=false)
	{
		if ($ignoreEncKey)
		{
			if (!isset(self::$_objs[$id]))
				self::$_objs[$id] = new ArchivingEndpoint($dbc, $org, $id, $ignoreEncKey);
			
			return self::$_objs[$id];
		}

		if (!isset(self::$_objs[$id]))
			self::$_objs[$id] = array();
		
		if (!isset(self::$_objs[$id][$org]))
			self::$_objs[$id][$org] = new ArchivingEndpoint($dbc, $org, $id, $ignoreEncKey);

		return self::$_objs[$id][$org];
	}

	/**
	 * updateFromDb
	 * 
	 * @param object $dbc          database connection object
	 * @param bool   $ignoreEncKey ignore enc key to optimise cache
	 *
	 * @return bool
	 */
	function updateFromDb($dbc, $ignoreEncKey)
	{
		if (!$ignoreEncKey)
		{
			$res = $dbc->query("SELECT EncryptionKey FROM Archiving.Provisioning WHERE OrgID={$this->org}");
			if (!($row = $res->fetch_object()))
				throw new Exception('Not provisioned yet');
			
			$this->encryptionKey = $row->EncryptionKey;
		}
		else
		{
			$this->encryptionKey = null;
		}
		
		LogDebug($sql = "SELECT Type, Encrypted, Compressed, RetentionManaged FROM Archiving.StorageEndpoint WHERE ID={$this->id}", 'SQL');
		$res = $dbc->query($sql);
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
			$res = $dbc->query("SELECT AccessKey, SecretKey, Bucket, Region FROM Archiving.StorageEndpointS3 WHERE ID={$this->id}");
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
			$res = $dbc->query("SELECT BasePath, SlowMedium FROM Archiving.StorageEndpointFileSystem WHERE ID={$this->id}");
			if ($row = $res->fetch_object())
			{
				$this->basePath = $row->BasePath;
				$this->slowMedium = $row->SlowMedium;

				$details_loaded = true;
			}
			break;

		case 's3-emulation':
			$res = $dbc->query("SELECT AccessKey, SecretKey, Bucket, BaseURI FROM Archiving.StorageEndpointS3Emulation WHERE ID={$this->id}");
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
	 * Update the data from API
	 * 
	 * @param array $api          api details (url, auth)
	 * @param bool  $ignoreEncKey ignore enc key to optimise cache
	 * 
	 * @return bool
	 */
	protected function updateFromApi($api, $ignoreEncKey)
	{
		$url = sprintf('%s/archiving/endpoint/%d/%d', $api[0], $this->org, $this->id);
		$auth = $api[1];
		
		$stream = HTTPStream::Make('GET', $url, null, array('Authorization'=>$auth));
		$streamKiller = new ScopeSentry(function() use (&$stream) {if ($stream) { $stream = null; unset($stream); }});
		if ($stream && $stream->httpCode == 200)
		{
			$xpath = new XPath($stream->getContents());
			$endpointNode = $xpath('/Archiving/Endpoint', true);
			if ($endpointNode)
				$endpointNode = $endpointNode->item(0);
			
			$this->encryptionKey = $ignoreEncKey? null: $xpath('EncryptionKey', false, $endpointNode);
			
			$this->type = $xpath('Type', false, $endpointNode);
			$this->encrypted = 'yes' == $xpath('Encrypted', false, $endpointNode);
			$this->compressed = 'yes' == $xpath('Compressed', false, $endpointNode);
			$this->retentionManaged = 'yes' == $xpath('RetentionManaged', false, $endpointNode);
			
			switch($this->type)
			{
				case 's3':
					$this->accessKey = $xpath('AccessKey', false, $endpointNode);
					$this->secretKey = $xpath('SecretKey', false, $endpointNode);
					$this->bucket = $xpath('Bucket', false, $endpointNode);
					$this->region = $xpath('Region', false, $endpointNode);
					break;
			
				case 'file-system':
					$this->basePath = $xpath('BasePath', false, $endpointNode);
					$this->slowMedium = $xpath('SlowMedium', false, $endpointNode);
					break;
			
				case 's3-emulation':
					$this->accessKey = $xpath('AccessKey', false, $endpointNode);
					$this->secretKey = $xpath('SecretKey', false, $endpointNode);
					$this->bucket = $xpath('Bucket', false, $endpointNode);
					$this->baseURI = $xpath('BaseURI', false, $endpointNode);
					break;
			}
			
			return true;
		}
		
		return false;
	}

	/**
	 * Constructor
	 *
	 * @param dbc  $dbc          dbc
	 * @param int  $rmOrgId      org
	 * @param int  $endpointID   id
	 * @param bool $ignoreEncKey ignore enc key to optimise cache
	 *
	 * @throws Exception
	 */
	function __construct($dbc, $rmOrgId, $endpointID, $ignoreEncKey)
	{
		$this->org = $rmOrgId;
		$this->id = $endpointID;
		
		if (false === ($ret = is_array($dbc)? $this->updateFromApi($dbc, $ignoreEncKey): $this->updateFromDb($dbc, $ignoreEncKey)))
			throw new Exception('Storage Endpoint not found');
	}
}
