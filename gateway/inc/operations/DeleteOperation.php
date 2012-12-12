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
 * DeleteOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class DeleteOperation extends ObjectOperation
{	
	/**
	 * Constructor
	 * 
	 * @param string   $bucket  S3 bucket name
	 * @param string   $key     Key
	 * 
	 * @throws StorageError
	 */
	public function __construct($bucket, $key)
	{
		parent::__construct($bucket, $key);

		if ($this->s3object->getObjectExists() === false)
		{
			LogError('Path does not exist', $this->id);
			throw new StorageError(StorageError::NoSuchKey, $this->id, Request::Get('id'));
		}
	}
	
	/**
	 * getHttpResponseStatus
	 * Returns an array containing the HTTP response code and message.
	 * The AWS documentation shows a slightly different response status for delete.
	 * 
	 * @return array
	 */
	public function getHttpResponseStatus()
	{
		return array('code' => '204', 'message' => 'NoContent');
	}
	
	/**
	 * doRequest
	 * Carries out the deletion by calling the appropriate S3Object function.
	 *
	 * @throws StorageError
	 *
	 * @return none
	 */
	public function doRequest()
	{
		if ($this->s3object->Delete() === false)
		{
			LogError('Deletion failed', $this->id);
			throw new StorageError(StorageError::InternalError, $this->id, Request::Get('id'));
		}
	}
}

}
