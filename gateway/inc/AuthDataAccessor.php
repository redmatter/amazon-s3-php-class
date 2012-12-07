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
namespace rmarchgateway
{

/**
 * Archiving Gateway
 * Interface for anything which provides a mechanism for returning the AWS Secret Access Key
 * and organisation associated with an AWS Access Key ID.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
interface AuthDataAccessor
{
	/**
	 * getAwsSecretAccessKey
	 * Get the AWS Secret Access Key associated with the passed AWS Access Key ID.
	 * Returns null if the AWS Access Key ID is not recognised or does not
	 * have an associated AWS Secret Access Key.
	 *
	 * @param  string    $awsAccessKeyId The AWS Access Key ID associated with the required AWS Secret Access Key
	 * 
	 * @return string
	 */
	public function getAwsSecretAccessKey($awsAccessKeyId);
	
	/**
	 * getDetailsForAwsKey
	 * This allows a user of the class to request whatever details it likes against a specified
	 * AWS Access Key ID, the function will populate the passed array with values against any keys
	 * that it can find.
	 *
	 * @param string $awsAccessKeyId        The AWS Access Key ID associated with the required details
	 * @param array  &$requiredDetailsArray Array with keys set to the details required
	 *
	 * @return none
	 */
	public function getDetailsForAwsKey($awsAccessKeyId, &$requiredDetailsArray);
}

}