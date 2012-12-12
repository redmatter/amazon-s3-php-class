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

use rmarchgateway\s3entities\S3Object;
use rmarchgateway\CopyObjectResult;
use rmarchgateway\renderables\StorageError;

use fluid\Request;

/**
 * PutCopyOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class PutCopyOperation extends PutUploadOperation
{
	protected $fromObject;
	
	/**
	 * Constructor
	 * 
	 * @param string   $toBucket     S3 Bucket to be copied to
	 * @param string   $toKey        Key to be copied to
	 * @param string   $fromBucket   S3 Bucket to be copied from
	 * @param string   $fromKey      Key to be copied from
	 * @param array    $metadata     Metadata to be used when storing copy, if needed to be different from original
	 *
	 * @throws StorageError
	 * @throws Exception
	 */
	public function __construct($toBucket, $toKey, $fromBucket, $fromKey, $metadata = null)
	{
		// Create an instance of an S3 object to be copied from.
		$this->fromObject = new S3Object($fromBucket, $fromKey);

		if (isset($this->metadata) === false)
		{
			$metadata = $this->fromObject->getMetadata();
		}
		
		// Get the file stream from the object.
		$stream = $this->fromObject->getFileStream();
		if (isset($stream) === false)
		{
			LogError('Object does not exist', $this->id);
			throw new StorageError(StorageError::NoSuchKey, $this->id, Request::Get('id'));
		}

		// Get the MD5 from the object
		$md5 = $this->fromObject->getMd5();
		if ($md5 === false)
		{
			LogError('Failed to calculate MD5');
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
			
		}

		// Call the parent constructor, which will create an instance of an S3 object to copy to.
		parent::__construct($toBucket, $toKey, $metadata, $stream, base64_encode($md5));
	}
	
	/**
	 * getHttpResponseHeaders
	 * Overridden to return nothing because AWS documentation doesn't specify any response headers which
	 * we intend to use.  The PutUploadOperation we derive from implements this function adding more headers,
	 * so to remove these headers we need to override it again.
	 * 
	 * @return array
	 */
	public function getHttpResponseHeaders()
	{
		$http_response_parameters = array();
		return $http_response_parameters;
	}
	
	/**
	 * getHttpBody
	 * Uses the data from the object we copied from to create XML for use in populating the body of the HTTP response.
	 *
	 * @throws Exception
	 * 
	 * @return string
	 */
	public function getHttpBody()
	{
		$copy_result = new CopyObjectResult(true, bin2hex($this->fromObject->getMd5()), $this->fromObject->getLastModified());
		return $copy_result->getXml();
	}
}

}