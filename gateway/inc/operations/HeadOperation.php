<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway\operations
{
use rmarchgateway\resources\Storage;
use rmarchgateway\renderables\StorageError;

use fluid\Request;

/**
 * HeadOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class HeadOperation extends ObjectOperation
{	
	/**
	 * Constructor
	 * 
	 * @param string   $bucket S3 bucket name
	 * @param string   $key    Key
	 * 
	 * @throws StorageError
	 */
	public function __construct($bucket, $key)
	{
		parent::__construct($bucket, $key);
		
		if ($this->s3object->getObjectExists() ===  false)
		{
			LogError('Object does not exist', $this->id);
			throw new StorageError(StorageError::NoSuchKey, $this->id, Request::Get('id'));
		}	
	}
	
	/**
	 * getHttpResponseHeaders
	 * Returns an array containing the HTTP response parameters as per AWS documentation.
	 *
	 * @throws StorageError
	 * @throws Exception
	 * 
	 * @return array
	 */
	public function getHttpResponseHeaders()
	{
		$http_response_parameters = array();
	
		$http_response_parameters[] = $this->getContentLength();
		$http_response_parameters[] = $this->getLastModified();
		$http_response_parameters[] = $this->getETag();
		$http_response_parameters = array_merge($http_response_parameters, $this->getMetadata());
		$http_response_parameters[] = $this->getContentType();
		return $http_response_parameters;
	}

	/**
	 * getContentLength
	 * Returns a Content-Length header based on the file length of the object.
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	protected function getContentLength()
	{
		$file_length = $this->s3object->getFileLength();
		if ($file_length === null)
		{
			LogError('File length could not be returned', $this->id);
			throw new \Exception('File length could not be returned');
		}

		return 'Content-Length: ' . $file_length;
	}

	/**
	 * getLastModified
	 * Returns a Last-Modified header based on the last modified date of the object. 
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	protected function getLastModified()
	{
		$lastModTime = $this->s3object->getLastModified();

		if (empty($lastModTime) === true)
		{
			LogError('Last modification time could not be returned', $this->id);
			throw new \Exception('Last modification time could not be returned');
		}
		
		return sprintf('Last-Modified: %s', $lastModTime->format(\DateTime::COOKIE));
	}
	
	/**
	 * getContentType
	 * Returns the Content-Type header parameters specifically using binary/octet-stream,
	 * if we don't do this manually Apache will put text/html instead.
	 * 
	 * @return string
	 */
	protected function getContentType()
	{
		return 'Content-Type: binary/octet-stream';
	}
	
	/**
	 * getMetadata
	 * Render the metadata parameters obtained from the .meta file.
	 *
	 * @throws Exception
	 * 
	 * @return array
	 */
	protected function getMetadata()
	{
		$meta_array = $this->s3object->getMetadata();
		$return_array = array();

		foreach ($meta_array as $key => $val)
		{
			$return_array[] = sprintf('%s%s: %s', Storage::META_HEADER_PREFIX, $key, $val);
		}

		return $return_array;
	}
	
	/**
	 * doRequest
	 * Implementation of abstract function.  Doesn't do anything, because nothing is 'done' to the
	 * object as part of this request.
	 *
	 * @return null
	 */
	public function doRequest()
	{
		return null;
	}
}

}
