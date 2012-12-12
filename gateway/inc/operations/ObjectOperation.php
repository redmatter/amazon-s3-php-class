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

/**
 * ObjectOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
abstract class ObjectOperation extends S3Operation
{	
	protected $s3object;
	
	/**
	 * Constructor
	 * 
	 * @param string $bucket S3 bucket name
	 * @param string $key    Key
	 * 
	 * @throws Exception
	 */
	public function __construct($bucket, $key)
	{
		parent::__construct("{$bucket}/{$key}");
		$this->s3object = new S3Object($bucket, $key);
	}
	
	/**
	 * getETag
	 * Get the ETag for an HTTP response, this is a hex representation of the MD5 of the object.
	 *
	 * @return string
	 */
	protected function getETag()
	{
		return sprintf('ETag: "%s"', bin2hex($this->s3object->getMd5()));
	}
}

}