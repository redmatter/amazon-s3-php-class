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

use rmarchgateway\s3entities\S3Bucket;

/**
 * BucketOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
abstract class BucketOperation extends S3Operation
{
	protected $bucket;
	
	/**
	 * Constructor
	 *
	 * @param string $bucket S3 bucket name
	 * 
	 * @throws StorageError
	 * @throws Exception
	 */
	public function __construct($bucket)
	{
		parent::__construct("{$bucket}");
		$this->bucket = new S3Bucket($bucket);
	}
}

}