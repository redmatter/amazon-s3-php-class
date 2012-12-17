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
namespace rmarchgateway\s3entities
{
	
use rmarchgateway\policy\RMBucketPolicyConfigAccessor;

use rmarchgateway\renderables\StorageError;

use \config;

/**
 * S3Bucket
 * Represents an S3 bucket.
 * Currently this class doesn't really do much and is just a placeholder, in the future
 * it may be responsible for determining policies, etc.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class S3Bucket
{
	protected $name;
	protected $path;
	protected $policyAccessor;

	/**
	 * Constructor
	 * 
	 * @param string $name S3 bucket name
	 *
	 * @throws StorageError
	 * @throws Exception
	 * 
	 * @return none
	 */
	public function __construct($name)
	{
		if (empty($name) === true)
		{
			LogError("No bucket name specified.");
			throw new StorageError(StorageError::InvalidBucketName);
		}
		
		$this->name = $name;
		$this->path = config::get('storage.base_path', '/tmp') . DIRECTORY_SEPARATOR . $name;
		$this->policyAccessor = new RMBucketPolicyConfigAccessor($this);
	}
	
	/**
	 * getName
	 *
	 * @return string
	 *
	 */
	public function getName()
	{
		return $this->name;
	}
	
	/**
	 * getPath
	 *
	 * @return string
	 */
	public function getPath()
	{
		return $this->path;
	}
	
	/**
	 * getPolicy
	 *
	 * @return RMBucketPolicy
	 */
	public function getPolicy()
	{
		return $this->policyAccessor->getPolicy();	
	}
}

}
