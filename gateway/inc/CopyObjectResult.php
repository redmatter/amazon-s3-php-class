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
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class CopyObjectResult
{
	protected $success;
	protected $eTag;
	protected $lastModified;
	
	/**
	 * Constructor
	 * 
	 * @param bool   $success      Indicates the success (true) or failure (false) of a copy
	 * @param string $eTag         An MD5 of the file which was copied
	 * @param string $lastModified The last modified date/time of the file which was copied
	 * 
	 */
	public function __construct($success, $eTag, $lastModified)
	{
		$this->success = $success;
		$this->eTag = $eTag;
		$this->lastModified = $lastModified;
	}
	
	/**
	 * getXml
	 * Returns an XML representation of the result, ready for returning as the HTTP body.
	 *
	 * @throws Exception
	 *
	 * @return string
	 */
	public function getXml()
	{
		if ($this->success === true)
		{
			$body = new \DOMMaker('CopyObjectResult');
			$this->addXml($body);
			return $body->getDOMDocument()->saveXML();
		}
		else
		{
			// There doesn't appear to be any XML returns for a failed copy, just a standard HTTP response would
			// be used, so we can just return an empty string.
			return '';
		}
	}
	
	/**
	 * addXml
	 * Appends the XML description of the success or failure of a copy, based on the variables passed during construction,
	 * to the passed XML message body.
	 * 
	 * @param \DOMMaker &$body The message body the result should be appended to. 
	 * 
	 * @throws Exception
	 * 
	 * @return none
	 */
	public function addXml(\DOMMaker &$body)
	{
		// AWS documentation doesn't appear to define XML to describe copy failures, so only add these nodes if the copy was successful.
		if ($this->success === true)
		{
			if (isset($this->eTag) === true)
			{
				$body->nodeCreate('ETag', '"' . $this->eTag . '"');
			}
			
			if (isset($this->lastModified) === true)
			{
				$body->nodeCreate('LastModified', $this->lastModified->format("Y-m-d\TH:i:s"));
			}
		}
	}
}

}