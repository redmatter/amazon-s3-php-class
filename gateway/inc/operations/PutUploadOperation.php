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

namespace rmarchgateway\operations
{

use rmarchgateway\renderables\StorageError;

use fluid\Request;

/**
 * PutUploadOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class PutUploadOperation extends ObjectOperation
{
	protected $metadata;
	protected $given_md5_base64;
	
	protected $stream = null;
	
	/**
	 * Constructor
	 * 
	 * @param string   $bucket     S3 bucket name
	 * @param string   $key        Key
	 * @param array    $metadata   Metadata to be stored during PUT
	 * @param resource $stream     Resource stream (the file to be PUT)
	 * @param string   $md5_base64 The MD5 hash of the resource
	 *
	 * @throws StorageError
	 * @throws Exception
	 */
	public function __construct($bucket, $key, $metadata, $stream, $md5_base64)
	{
		parent::__construct($bucket, $key);
		LogError("PUT constructor: " . $this->s3object->getDescription());

		$this->metadata = $metadata;
		$this->given_md5_base64 = $md5_base64;
		$this->stream = $stream;

		if (is_null($stream))
		{
			LogError('Stream was NULL', $this->id);
			throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
		}

		if (is_null($md5_base64))
		{
			LogError('No MD5 given', $this->id);
			throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
		}

		if (is_null($metadata) || (is_array($metadata) && !count($metadata)))
		{
			LogError('No Metadata given', $this->id);
			throw new StorageError(StorageError::PreconditionFailed, $this->id, Request::Get('id'));
		}
	}
	
	/**
	 * doRequest
	 * Performs the actual request, using the S3 object representation to write the stream to disk.
	 *
	 * @throws StorageError
	 * @throws Exception
	 *
	 * @return none
	 */
	public function doRequest()
	{
		LogError("PUT write: " . $this->s3object->getDescription());
		$this->s3object->Write($this->stream, $this->metadata);
		LogError("PUT commit: " . $this->s3object->getDescription());
		$this->s3object->Commit($this->given_md5_base64);
		LogError("PUT end: " . $this->s3object->getDescription());
	}
	
	/**
	 * getHttpResponseHeaders
	 * Returns an array containing the HTTP response parameters.
	 * 
	 * @return array
	 */
	public function getHttpResponseHeaders()
	{
		$http_response_parameters = array();
		$http_response_parameters[] = $this->getETag();
		return $http_response_parameters;
	}
}

}
