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

use rmarchgateway\renderables\GetOK;
use rmarchgateway\renderables\MultiDeleteOK;

use rmarchgateway\S3Authentication;
use rmarchgateway\StorageItem;
use rmarchgateway\StorageItems;

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
		{
			if (0 === stripos($header, self::META_HEADER_PREFIX))
			{
					$metadata[substr($header, self::META_HEADER_PREFIX_LENGTH)] = $value;
			}
		}
		
		$given_md5_base64 = isset($this->client->request->headers['Content-MD5'])? $this->client->request->headers['Content-MD5']: null;
				
		$item = new StorageItem(StorageItem::USAGE_PUT, $this->params[0], $this->params[2], $this->params[1], $this->params[3], $metadata, $this->client->request->data->as_stream, $given_md5_base64);
		$item->Write();
		$item->Commit();
		
		return new StorageItemResponse($item, $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleDelete()
	 */
	public function HandleDelete()
	{
		// Create StorageItem object to represent the file being deleted based on the content of the HTTP DELETE request.
		$item = new StorageItem(StorageItem::USAGE_DELETE, $this->params[0], $this->params[2], $this->params[1], $this->params[3]);
		
		// Delete the item.
		$item->Delete();
		
		// Return an HTTP response to the delete request indicating that it was successful.
		// If it failed, an exception will have been raised, causing it not to reach this stage.
		return new StorageItemResponse($item, $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleGet()
	 */
	public function HandleGet()
	{
		return new GetOK(new StorageItem(StorageItem::USAGE_GET, $this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleHead()
	 */
	public function HandleHead()
	{
		return new StorageItemResponse(new StorageItem(StorageItem::USAGE_HEAD, $this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandlePost()
	 */
	public function HandlePost()
	{
		// POST can be used for a number of reasons.  Currently we only support multi delete.
		if ($this->client->request->query_string == 'delete')
		{
			$given_md5_base64 = isset($this->client->request->headers['Content-MD5']) ? $this->client->request->headers['Content-MD5'] : null;
			$item = new StorageItems(StorageItem::USAGE_MULTI_DELETE, $this->params[0], $this->client->request->data->as_string, $given_md5_base64);
			
			$item->Delete();
			
			return new MultiDeleteOK($item, $this->client->request->id);
		}
		// The parent implementation simply returns a not implemented exception, so call this if we don't know how to handle it.
		else
		{
			parent::HandlePost();
		}
	}
	
	/**
	 * GetResourceInstance
	 * 
	 * @param array         $params        parameters
	 * @param \fluid\Client &$client       client
	 * @param string        $resource_type resource type
	 * 
	 * @return \fluid\Resource
	 */
	public static function GetResourceInstance($params, \fluid\Client &$client, $resource_type=null)
	{
		return new self($params, $client);
	}
	
	public function IsAuthed()
	{
		LogDebug("Authenticating...");
		$authenticator = new S3Authentication(
					$this->client->request->method,
					$this->params,
					$this->client->request->query_string,
					$this->client->request->headers
					);
			
		return $authenticator->checkAuthentication();
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