<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway\renderables
{

use rmarchgateway\StorageItem;

/**
 * Archiving Gateway
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class PutOK extends StorageError
{
	/**
	 * Ctor
	 * 
	 * @param StorageItem $item      storage item
	 * @param string      $requestId request-id
	 */
	public function __construct(StorageItem $item, $requestId)
	{
		$etag = bin2hex($item->md5);
		parent::__construct(StorageError::PutOK, $item, $requestId, array("ETag: {$etag}"));
	}
}

}