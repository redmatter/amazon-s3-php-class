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
class DeleteResult
{
	protected $success;
	protected $key;
	protected $code;
	protected $message;
	protected $versionId;
	protected $deleteMarker;
	protected $deleteMarkerVersionId;
	
	/**
	 * Ctor
	 * 
	 * @param bool   $success               Indicates the success (true) or failure (false) of a deletion
	 * @param string $key                   Key for the object which was attempted to be deleted
	 * @param string $code                  Status code for the result of a failed delete
	 * @param string $message               Error description
	 * @param string $versionId             VersionId for the versioned object in the case of a versioned delete
	 * @param bool   $deleteMarker          Indicates if the request accessed a delete marker (true) or not (false)
	 * @param string $deleteMarkerVersionId VersionId of the delete marker if one was deleted by the request
	 * 
	 * @return none
	 */
	public function __construct($success, $key, $code = null, $message = null, $versionId = null, $deleteMarker = null, $deleteMarkerVersionId = null)
	{
		$this->success = $success;
		$this->key = $key;
		$this->code = $code;
		$this->message = $message;
		$this->versionId = $versionId;
		$this->deleteMarker = $deleteMarker;
		$this->deleteMarkerVersionId = $deleteMarkerVersionId;
	}
	
	/**
	 * addXml
	 * Appends the XML description of the success or failure of a delete, based on the variables passed during construction,
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
		if ($this->success === true)
		{
			$delete_node = $body->nodeCreate('Deleted');

			if (isset($this->key) === true)
				$body->nodeCreate('Key', $this->key, $delete_node);
			
			if (isset($this->versionId) === true)
				$body->nodeCreate('VersionId', $this->versionId, $delete_node);
			
			if (isset($this->deleteMarker) === true)
				$body->nodeCreate('DeleteMarker', $this->deleteMarker, $delete_node);
			
			if (isset($this->deleteMarkerVersionId) === true)
				$body->nodeCreate('DeleteMarkerVersionId', $this->deleteMarkerVersionId, $delete_node);
		}
		else
		{
			$error_node = $body->nodeCreate('Error');
			
			if (isset($this->key) === true)
				$body->nodeCreate('Key', $this->key, $error_node);
				
			if (isset($this->versionId) === true)
				$body->nodeCreate('VersionId', $this->versionId, $error_node);
				
			if (isset($this->code) === true)
				$body->nodeCreate('Code', $this->code, $error_node);
				
			if (isset($this->message) === true)
				$body->nodeCreate('Message', $this->message, $error_node);
		}
	}
}

}