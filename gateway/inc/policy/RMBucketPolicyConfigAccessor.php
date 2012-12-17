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
use rmarchgateway\XmlConfigFileAccessor;

/**
 * RMBucketPolicyConfigAccessor
 * Implementation of the RMBucketPolicyAccessor interface allowing access to a policy held
 * in an XML configuration file.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class RMBucketPolicyConfigAccessor extends XmlConfigFileAccessor implements RMBucketPolicyAccessor
{
	const POLICY_FILE_NAME = 'policy.xml';
	
	protected $policy;
	
	/**
	 * Constructor
	 * Determines the policy file path and passes it to the parent constructor.
	 *
	 * @throws Exception
	 *
	 * @return none
	 */
	public function __construct(S3Bucket $bucket)
	{
		if (isset($bucket) === false)
		{
			LogError('$bucket is null');
			throw new \Exception('$bucket is null');
		}

		if (!$bucket->getPath())
		{
			LogError('$bucket->getPath is empty');
			throw new \Exception('$bucket->getPath is empty');
		}

		parent::__construct($bucket->getPath() . DIRECTORY_SEPARATOR . self::POLICY_FILE_NAME);
	}

	/**
	 * getPolicy
	 * Get the policy associated with the bucket.
	 * Sets the policy to a member variable so this only gets done once.
	 * 
	 * @return RMBucketPolicy
	 */
	public function getPolicy()
	{
		if (isset($this->policy) === false)
		{
			$xml_root = $this->getRootXPath();
			
			$this->policy = new RMBucketPolicy();
			
			$rules = $xml_root('/Policy/Rule', true);
			if (empty($rules) === false)
			{
				foreach($rules as $rule)
				{
					// Add rule to policy.
					$name = $rule->attributes->getNamedItem('name')->nodeValue;
					$type = $rule->attributes->getNamedItem('type')->nodeValue;
					$value = $rule->nodeValue;
						
					$this->policy->addRule(new RMBucketPolicyRule($name, $type, $value));
				}
			}
		}

		return $this->policy;
	}
	
	/**
	 * setPolicy
	 * Set the policy for the bucket.
	 * Currently not implemented.
	 *
	 * @param RMBucketPolicy $policy The policy to be set
	 *
	 * @return none
	 */
	public function setPolicy(RMBucketPolicy $policy)
	{
		LogError('Function not currently implemented');
		throw new \Exception('NOT IMPLEMENTED - setPolicy');
	}
}

}