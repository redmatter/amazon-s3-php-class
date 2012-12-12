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

use fluid\RenderSource;

/**
 * S3Operation
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
abstract class S3Operation implements RenderSource
{
	public $id;
	protected $lc;
	
	/**
	 * Constructor
	 * 
	 * @param string $id ID for this operation
	 */
	public function __construct($id)
	{
		$this->id = "/{$id}/";
		$this->lc = new \SysLogContext($this->id);
	}
	
	/**
	 * getHttpResponseStatus
	 * Returns an array containing the HTTP response code and message.
	 * Default implementation of this RenderSource interface function which most
	 * child classes can use.  If something different is needed the child must
	 * override it.
	 *
	 * @return array
	 */
	public function getHttpResponseStatus()
	{
		return array('code' => '200', 'message' => 'OK');
	}
	
	/**
	 * getHttpResponseHeaders
	 * Returns an empty array.
	 * Default implementation of this RenderSource interface function which most
	 * child classes can use.  If additional headers are needed the child must
	 * override it.
	 *
	 * @return array
	 */
	public function getHttpResponseHeaders()
	{
		$http_response_parameters = array();
		return $http_response_parameters;
	}
	
	/**
	 * getHttpBody
	 * Default definition of the RenderSource interface function, returns null, to save child classes
	 * who don't return a body from needing to define it.
	 *
	 * @return null
	 */
	public function getHttpBody()
	{
		return null;
	}
	
	/**
	 * doRequest
	 * Abstract function to encourage all operations to use the same format for actually performing the request.
	 *
	 * @return none
	 */
	abstract public function doRequest();

}

}