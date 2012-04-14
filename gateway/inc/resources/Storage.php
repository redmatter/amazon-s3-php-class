<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway\resources
{
	
use rmarchgateway\renderables\StorageItemResponse;

use rmarchgateway\renderables\PutOK;

use rmarchgateway\StorageItem;

/**
 * Storage
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Storage extends \fluid\Resource
{
	const META_HEADER_PREFIX = 'x-amz-meta-';
	const META_HEADER_PREFIX_LENGTH = 11;
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandlePut()
	 */
	public function HandlePut()
	{
		$metadata = array();
		foreach ($this->client->request->headers as $header => $value)
			if (0 === stripos($header, self::META_HEADER_PREFIX))
				$metadata[substr($header, self::META_HEADER_PREFIX_LENGTH)] = $value;
		
		$given_md5_base64 = isset($this->client->request->headers['Content-MD5'])? $this->client->request->headers['Content-MD5']: null;
				
		$item = new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3], $metadata, $this->client->request->data->as_stream, $given_md5_base64);
		$item->Write();
		$item->Commit();

		return new PutOK($item, $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleGet()
	 */
	public function HandleGet()
	{
		return new StorageItemResponse(new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id, true, true);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleHead()
	 */
	public function HandleHead()
	{
		return new StorageItemResponse(new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id, false, true);
	}
	
	/**
	 * 
	 * @param array         $params
	 * @param \fluid\Client $client
	 * @param string        $resource_type
	 * 
	 * @return \fluid\Resource
	 */
	public static function GetResourceInstance($params, \fluid\Client &$client, $resource_type=null)
	{
		return new self($params, $client);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::NeedAuth()
	 */
	public function NeedAuth()
	{
		return false;
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::IsAllowedDirectRouting()
	 */
	public function IsAllowedDirectRouting()
	{
		return true;
	}
}
	
}