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

use rmarchgateway\DeleteResult;

use rmarchgateway\renderables\StorageError;

use fluid\Request;
use fluid\HttpException;


/**
 * DeleteMultiOperation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class DeleteMultiOperation extends BucketOperation
{
	protected $xmlString;
	protected $keys;
	
	/**
	 * Constructor
	 * Initialises member variables, checks the MD5 of the XML string.
	 * 
	 * @param string $bucket     S3 bucket name
	 * @param string $xml_string XML string describing the request
	 * @param string $md5_base64 The MD5 hash of the XML string
	 * 
	 * @throws StorageError
	 */
	public function __construct($bucket, $xml_string, $md5_base64 = null)
	{
		parent::__construct($bucket);
		
		$this->xmlString = $xml_string;
		
		// If we don't have a stream we can't proceed
		if (isset($this->xmlString) === false)
		{
			LogError('No XML', $this->id);
			throw new StorageError(StorageError::MissingRequestBodyError, $this->id, Request::Get('id'));
		}

		// Check that the supplied MD5 corresponds with the supplied message body.
		if (isset($md5_base64))
		{
			if ($md5_base64 != base64_encode(md5($this->xmlString, true)))
			{
				LogError('Supplied MD5 did not correspond with message body', $this->id);
				throw new StorageError(StorageError::BadDigest, $this, Request::Get('id'));
			}
		}
	}

	/**
	 * doRequest
	 * Uses the XML passed during construction to get the list of keys to be deleted.
	 * For each key, a StorageItem is created, then Delete is called on the StorageItem, to
	 * delete the resource.
	 * Any exceptions thrown during construction of StorageItem or during the delete are caught
	 * and used to define the <Error> tags in the response XML. 
	 *
	 * @throws StorageError
	 * 
	 * @return none
	 */
	public function doRequest()
	{
		try
		{
			$meta_objects = new \XPath($this->xmlString);
		}
		catch (Exception $e)
		{
			LogError('Error parsing XML', $this->id);
			throw new StorageError(StorageError::MalformedXML, $this->id, Request::Get('id'));
		}
		
		$this->keys = array();
		
		// Get each 'Key' element from the XML, use it to populate a DeleteOperation, then use the DeleteOperation to delete the resource.
		foreach ($meta_objects('Object/Key', true) as $key)
		{
			try
			{
				$item = new DeleteOperation($this->bucket->getName(), $key->nodeValue);
				$item->doRequest();
			
				// If we reached this line (DeleteOperation constructor and doRequest function didn't throw an exception) then we must have been
				// successful, so create a successful result entry.
				$this->keys[$key->nodeValue] = new DeleteResult(true, $key->nodeValue);
			}
			// We catch the exceptions here and, rather than return them to be rendered, we just set our result for this specific deletion
			// based on the content of the exception.
			catch (StorageError $e)
			{
				$aws_code = $e->getAwsCode();
				$this->keys[$key->nodeValue] = new DeleteResult(false, $key->nodeValue, $aws_code, $e->getMessageFromCode($aws_code));
			}
			catch (HttpException $e)
			{
				$this->keys[$key->nodeValue] = new DeleteResult(false, $key->nodeValue, $e->getCode(), $e->getMessage());
			}
			catch (Exception $e)
			{
				$this->keys[$key->nodeValue] = new DeleteResult(false, $key->nodeValue, $e->getCode(), $e->getMessage());
			}
		}

		if (empty($this->keys) === true)
		{
			// If we didn't find any keys in the XML then something has gone wrong, throw an exception.
			LogError('No keys found in array', $this->id);
			throw new StorageError(StorageError::MalformedXML, $this->id, Request::Get('id'));
		}
	}
	
	/**
	 * getHttpResponseHeaders
	 * Returns an array containing the HTTP response parameters.
	 *
	 * @return array
	 */
	public function getHttpResponseHeaders()
	{
		return array('Content-Type: application/xml');
	}
	
	/**
	 * getHttpBody
	 * Uses the results of the deletes to create the XML HTTP response body.
	 *
	 * @throws Exception
	 *
	 * @return none
	 */
	public function getHttpBody()
	{
		$body = new \DOMMaker('DeleteResult');
		foreach ($this->keys as $key => $val)
		{
			$val->addXml($body);
		}
	
		return $body->getDOMDocument()->saveXML();
	}

}

}