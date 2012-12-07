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

use rmarchgateway\AuthDataAccessor;

use \config;
	
/**
 * Archiving Gateway
 * Implementation of AuthDataAccessor which uses the standard configuration
 * file to retrieve the AWS Secret Access Key and any other details for a
 * specified AWS Access Key ID.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class AuthDataConfigAccessor implements AuthDataAccessor
{
	protected $authDataFilePath;
	
	/**
	 * Constructor
	 * Gets the authentication data file path from the configuration file.
	 * 
	 * @throws Exception
	 *
	 * @return none
	 */
	public function __construct()
	{
		$data_file_path = config::get('authentication.data_file_path', 'not_set');

		if ($data_file_path === 'not_set')
		{
			LogError("Configuration property: 'authentication.data_file_path' could not be found.");
			throw new \Exception("Configuration property: 'authentication.data_file_path' could not be found.");
		}
		
		$this->authDataFilePath = $data_file_path;
	}

	/**
	 * getAwsSecretAccessKey
	 * Get the AWS Secret Access Key associated with the passed AWS Access Key ID.
	 * Returns null if the AWS Access Key ID is not recognised or does not
	 * have an associated AWS Secret Access Key.
	 *
	 * @param string $awsAccessKeyId The AWS Access Key ID associated with the required AWS Secret Access Key
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	public function getAwsSecretAccessKey($awsAccessKeyId)
	{
		return $this->getValueForAwsKey($this->getXmlValueForAwsAccessKeyId($awsAccessKeyId), 'AWSSecretAccessKey');
	}

	/**
	 * getDetailsForAwsKey
	 * This allows a user of the class to request whatever details it likes against a specified
	 * AWS Access Key ID, the function will populate the passed array with values against any keys
	 * that it can find.
	 * 
	 * @param string $awsAccessKeyId        The AWS Access Key ID associated with the required details
	 * @param array  &$requiredDetailsArray Array with keys set to the details required
	 *
	 * @throws Exception
	 *
	 * @return none
	 */
	public function getDetailsForAwsKey($awsAccessKeyId, &$requiredDetailsArray)
	{
		if (isset($requiredDetailsArray) === false)
		{
			LogError('No required details were specified');
			throw new \Exception('No required details were specified');
		}
		
		if (is_array($requiredDetailsArray) === false)
		{
			LogError('Required details should have been specified as an array, but were not');
			throw new \Exception('Required details should have been specified as an array, but were not');
		}
		
		$access_key_node = $this->getXmlValueForAwsAccessKeyId($awsAccessKeyId);
		
		foreach ($requiredDetailsArray as $key => $val)
		{
			$requiredDetailsArray[$key] = $this->getValueForAwsKey($access_key_node, $key);
		}
	}
	
	/**
	 * getXmlNodeForAwsAccessKeyId
	 * Returns a DOMNode containing the XML child elements of the AWS Access Key associated with
	 * the passed AWS Access Key ID.
	 * Returns null if a matching node is not found.
	 *
	 * @param string $awsAccessKeyId The AWS Access Key ID associated with the required AWS Access Key.
	 *
	 * @throws Exception
	 *
	 * @return DOMNode
	 */
	protected function getXmlValueForAwsAccessKeyId($awsAccessKeyId)
	{
		// Check we were passed a populated AWS Access Key ID.
		if (empty($awsAccessKeyId))
		{
			LogError('No AWS Access Key ID specified');
			throw new \Exception('No AWS Access Key ID specified');
		}
		
		// Get an XPath representation of the authentication file, then look in it for an AWSAccessKey element
		// with matching ID.
		$xml_root = $this->getRootXPath();
		$access_key_node = $xml_root("/AWSAccessKeys/AWSAccessKey[@id='$awsAccessKeyId']", true);

		// Check that we got something back.
		if (isset($access_key_node) === false)
		{
			LogWarning("Failed to return AWSAccessKey element with the correct ID ($awsAccessKeyId) when parsing XML");
			return null;
		}
		
		// See if we found any matching AWS Access Keys.
		if ($access_key_node->length === 0)
		{
			LogWarning("No AWSAccessKey elements found with the correct ID ($awsAccessKeyId) when parsing XML");
			return null;
		}
		
		// Check we didn't get too many matching AWS Access Keys.
		if ($access_key_node->length > 1)
		{
			LogWarning("Too many ($access_key_node->length) AWSAccessKey elements found with the correct ID ($awsAccessKeyId) when parsing XML");
			return null;
		}
		
		return $access_key_node->item(0);
	}

	/**
	 * getRootXPath
	 * Returns an XPath representation of the root of the authentication data file.
	 *
	 * @throws Exception
	 *
	 * @return \XPath
	 */
	protected function getRootXPath()
	{
		$xml_file_content = $this->getXmlContentFromFile();
	
		// Try turning the content of the file in to an XPath, catch and re-throw the exception if not.
		try
		{
			return new \XPath($xml_file_content);
		}
		catch(\Exception $e)
		{
			LogError("Failed to parse XML: $xml_file_content");
			throw new \Exception("Failed to parse XML: $xml_file_content", $e->getCode(), $e);
		}
	}

	/**
	 * getXmlContentFromFile
	 * Returns a string containing the content of the authentication details XML file.
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	protected function getXmlContentFromFile()
	{
		// Check that the authentication data file exists.
		if (is_file($this->authDataFilePath) === false)
		{
			LogError("Authentication file could not be found: $this->authDataFilePath");
			throw new \Exception("Authentication file could not be found: $this->authDataFilePath");
		}

		// Get the content of the file and check it didn't fail.
		$xml_file_content = @file_get_contents($this->authDataFilePath);
		if ($xml_file_content === false)
		{
			LogError("Failed to get valid XML content from file: $this->authDataFilePath");
			throw new \Exception("Failed to get valid XML content from file: $this->authDataFilePath");
		}

		// Check the file actually contained something.
		if (empty($xml_file_content) === true)
		{
			LogError("XML file was empty: $this->authDataFilePath");
			throw new \Exception("XML file was empty: this->authDataFilePath");
		}

		return $xml_file_content;
	}
	
	/**
	 * getValueForAwsKey
	 * Returns the content of the specified node from within the passed DOMNode.
	 * Returns null and logs a warning if the specified node is not found or if there
	 * is more than one of the specified node.
	 * 
	 * @param DOMNode $xmlNode     The XML node to be searched
	 * @param string  $elementName The element to be returned
	 *
	 * @return string
	 */
	protected function getValueForAwsKey($xmlNode, $elementName)
	{		
		if (empty($xmlNode))
		{
			LogWarning("Passed xmlNode is empty.");
			return null;
		}
		
		if (($xmlNode instanceof \DOMNode) === false)
		{
			LogWarning("Passed xmlNode is not an instance of DOMNode.");
			return null;
		}

		$matching_nodes = $xmlNode->getElementsByTagName($elementName);

		if (empty($matching_nodes))
		{
			LogWarning("Node '$elementName' not found in passed xmlNode: " . print_r($xmlNode, true));
			return null;
		}
		
		if ($matching_nodes->length === 0)
		{
			LogWarning("Node '$elementName' not found in passed xmlNode: " . print_r($xmlNode, true));
			return null;
		}
		
		if ($matching_nodes->length > 1)
		{
			LogWarning("Too many nodes with element name '$elementName' found in passed xmlNode: " . print_r($xmlNode, true));
			return null;
		}

		return $matching_nodes->item(0)->nodeValue;
	}
}

}