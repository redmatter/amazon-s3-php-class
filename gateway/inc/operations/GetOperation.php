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

/**
 * GetOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
use rmarchgateway\renderables\StorageError;

class GetOperation extends HeadOperation
{
	/**
	 * getHttpBody
	 * Return the HTTP Body, a stream of the file content.
	 * Throws a StorageError if the stream can not be returned from the object.
	 *
	 * @throws StorageError
	 *
	 * @return resource
	 */
	public function getHttpBody()
	{
		$stream = $this->s3object->getFileStream();
		
		if (isset($stream) === false)
		{
			LogError('Failed to get file stream');
			throw new \Exception('Failed to get file stream');
		}
		else
		{
			return $stream;
		}
	}
}

}
