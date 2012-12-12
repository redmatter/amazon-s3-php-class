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

use rmarchgateway\operations\PutCopyOperation;

use rmarchgateway\renderables\StorageItemResponse;

use rmarchgateway\renderables\GetOK;
use rmarchgateway\renderables\MultiDeleteOK;

use rmarchgateway\operations\DeleteMultiOperation;
use rmarchgateway\operations\DeleteOperation;
use rmarchgateway\operations\GetOperation;
use rmarchgateway\operations\HeadOperation;
use rmarchgateway\operations\PutUploadOperation;

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
		
		$bucket_and_key = $this->getBucketAndKeyFromParams($this->params);

		// PUT is used for uploading files, but also for copying files.  First we need to check what this PUT is for.
		// If the 'x-amz-copy-source' header is present, this is actually a copy request.
		if (isset($this->client->request->headers['x-amz-copy-source']) === true)
		{
			$copy_source_array = $this->getBucketAndKeyFromParams($this->client->request->headers['x-amz-copy-source']);
			$item = new PutCopyOperation($bucket_and_key['bucket'], $bucket_and_key['key'], $copy_source_array['bucket'], $copy_source_array['key'], $metadata);
		}
		else
		{
			$given_md5_base64 = isset($this->client->request->headers['Content-MD5'])? $this->client->request->headers['Content-MD5']: null;
			$item = new PutUploadOperation($bucket_and_key['bucket'], $bucket_and_key['key'], $metadata, $this->client->request->data->as_stream, $given_md5_base64);
		}

		return new StorageItemResponse($item, $this->client->request->id);
	}

	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleDelete()
	 */
	public function HandleDelete()
	{
		$bucket_and_key = $this->getBucketAndKeyFromParams($this->params);
		return new StorageItemResponse(new DeleteOperation($bucket_and_key['bucket'], $bucket_and_key['key']), $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleGet()
	 */
	public function HandleGet()
	{
		$bucket_and_key = $this->getBucketAndKeyFromParams($this->params);
		return new StorageItemResponse(new GetOperation($bucket_and_key['bucket'], $bucket_and_key['key']), $this->client->request->id);
	}
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::HandleHead()
	 */
	public function HandleHead()
	{
		$bucket_and_key = $this->getBucketAndKeyFromParams($this->params);
		return new StorageItemResponse(new HeadOperation($bucket_and_key['bucket'], $bucket_and_key['key']), $this->client->request->id);
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
			$bucket_and_key = $this->getBucketAndKeyFromParams($this->params);
			$given_md5_base64 = isset($this->client->request->headers['Content-MD5']) ? $this->client->request->headers['Content-MD5'] : null;
			return new StorageItemResponse(new DeleteMultiOperation($bucket_and_key['bucket'], $this->client->request->data->as_string, $given_md5_base64), $this->client->request->id);
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
	
	/**
	 * (non-PHPdoc)
	 * @see fluid.Resource::IsAuthed()
	 */
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
	
	/**
	 * getBucketAndKeyFromParams
	 * Takes a 'params' object intended to be either a string of format '/bucket/an-s3/id' or an array, based on an exploded
	 * URI string of format '/bucket/elements/of/key' and returns an array containing elements 'bucket' and 'key', populated appropriately.
	 * The first format is made up of a bucket before the first /, or second / if the first character is a /, characters after
	 * the second / are the key and can be made up of any characters, including /.
	 *
	 * @param mixed  $params Bucket and Key values represented as either an array or string.
	 *
	 * @return array
	 */
	protected function getBucketAndKeyFromParams($params)
	{
		$return_array = array();

		// If we're being passed the array of parameters from the Client class then the first should be the bucket, the remaining
		// elements should be recombined (if split) to form the key.
		if (is_array($params) === true)
		{
			$return_array['bucket'] = array_shift($params);
			$return_array['key'] = implode('/', $params);
		}
		// Otherwise this has been passed from another header, in which case the string between the first two forward slashes should
		// be treated as the bucket. 
		else
		{
			if (preg_match('~(?<=/)[[:graph:]^/]*(?=/)~', $params, $bucket_result) === 1)
			{
				$return_array['bucket'] = $bucket_result[0];
			}

			if (preg_match('~(?<=' . $return_array['bucket'] . '/)[[:graph:]]*$~', $params, $key_result) === 1)
			{
				$return_array['key'] = $key_result[0];
			}
		}

		return $return_array;
	}
}
	
}