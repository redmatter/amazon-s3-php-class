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

	/**
	 * Constructor
	 * 
	 * @param string $name S3 bucket name
	 * 
	 * @throws Exception
	 * 
	 */
	public function __construct($name)
	{
		$this->name = $name;
		$this->path = config::get('storage.base_path', '/tmp') . DIRECTORY_SEPARATOR . $name;
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
	 *
	 */
	public function getPath()
	{
		return $this->path;
	}
}

}
