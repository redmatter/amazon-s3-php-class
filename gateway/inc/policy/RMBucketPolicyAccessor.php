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
namespace rmarchgateway\policy
{

use rmarchgateway\s3entities\S3Bucket;

/**
 * RMBucketPolicyAccessor
 * Interface for anything which provides a mechanism for returning the policy for
 * a bucket.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
interface RMBucketPolicyAccessor
{
	/**
	 * getPolicy
	 * Get the policy associated with the bucket.
	 * 
	 * @return RMBucketPolicy
	 */
	public function getPolicy();
	
	/**
	 * setPolicy
	 * Set the policy for the bucket.
	 *
	 * @param RMBucketPolicy $policy The policy to be set
	 *
	 * @return none
	 */
	public function setPolicy(RMBucketPolicy $policy);
}

}