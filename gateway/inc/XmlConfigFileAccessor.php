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
 * XmlConfigFileAccessor
 * Abstract class describing something which is used to access an XML configuration file.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
abstract class XmlConfigFileAccessor
{
	protected $xmlConfigFilePath;
	
	/**
	 * Constructor
	 * Gets the authentication data file path from the configuration file.
	 * 
	 * @throws Exception
	 *
	 * @return none
	 */
	public function __construct($xmlConfigFilePath)
	{
		
		if (empty($xmlConfigFilePath) === true)
		{
			LogError('$xmlConfigFilePath is empty');
			throw new \Exception('$xmlConfigFilePath is empty');
		}
		
		$this->xmlConfigFilePath = $xmlConfigFilePath;
	}

	/**
	 * getRootXPath
	 * Returns an XPath representation of the root of the XML configuration data file.
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
	 * Returns a string containing the content of the XML configuration data file.
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	protected function getXmlContentFromFile()
	{
		// Check that the XML configuration data file exists.
		if (is_file($this->xmlConfigFilePath) === false)
		{
			LogError("XML configuration data file could not be found: $this->xmlConfigFilePath");
			throw new \Exception("XML configuration data file could not be found: $this->xmlConfigFilePath");
		}

		// Get the content of the file and check it didn't fail.
		$xml_file_content = @file_get_contents($this->xmlConfigFilePath);
		if ($xml_file_content === false)
		{
			LogError("Failed to get valid XML content from file: $this->xmlConfigFilePath");
			throw new \Exception("Failed to get valid XML content from file: $this->xmlConfigFilePath");
		}

		// Check the file actually contained something.
		if (empty($xml_file_content) === true)
		{
			LogError("XML file was empty: $this->xmlConfigFilePath");
			throw new \Exception("XML file was empty: this->xmlConfigFilePath");
		}

		return $xml_file_content;
	}
}

}