<?php
/**
* Archiving Retention
*
* PHP version 5
*
* @category  MySQLiErrorException
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 Red Matter Ltd (UK)
* @license   http://www.redmatter.com/license private
* @version   SVN: <svn_id>
* @link      http://www.redmatter.com
*/

/**
 * MySQLiErrorException
 *
 * @category  MySQLiErrorException
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class MySQLiErrorException extends Exception
{
	private $_conn;
	
	/**
	 * Get conn
	 * 
	 * @return conn
	 */
	public function getConnection()
	{
		return $this->_conn;
	}
	
	/**
	 * Ctor
	 * 
	 * @param string $message  message
	 * @param int    $errno    errno
	 * @param conn   $conn     conn
	 * @param Except $previous prev
	 */
	public function __construct($message, $errno, $conn, $previous=null)
	{
		$this->_conn = $conn;
		parent::__construct($message, $errno, $previous);
	}
}