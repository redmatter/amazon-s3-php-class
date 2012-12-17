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

/**
 * RMBucketPolicy
 * Class which encapsulates a RedMatter Bucket Policy.
 * These policies are different from Amazon AWS bucket policies, which don't
 * support all the constraints that we need.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class RMBucketPolicy
{
	protected $rules = array();

	/**
	 * getRule
	 * Get the rule with the specified name.
	 *
	 * @return RMBucketPolicyRule
	 */
	public function getRule($name)
	{
		return $this->rules[$name];
	}
	
	/**
	 * getRules
	 * Get all rules.
	 *
	 * @return array
	 */
	public function getRules()
	{
		return $this->rules;
	}

	/**
	 * setRule
	 * Add a rule to the collection.
	 *
	 * @param RMBucketPolicyRule $rule The rule to add
	 *
	 * @return none
	 */
	public function addRule(RMBucketPolicyRule $rule)
	{
		$this->rules[$rule->getName()] = $rule;
	}

	/**
	 * setRules
	 * Set a collection of rules.
	 *
	 * @param array $rules The collection of rules
	 *
	 * @return none
	 */
	public function setRules($rules)
	{
		$this->rules = $rules;
	}
}

}