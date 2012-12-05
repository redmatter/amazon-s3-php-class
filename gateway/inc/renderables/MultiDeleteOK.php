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
namespace rmarchgateway\renderables
{

use fluid\RenderSource;

use rmarchgateway\renderables\StorageItemResponse;

use rmarchgateway\StorageItem;

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
class MultiDeleteOK extends StorageItemResponse
{
	/**
	 * Ctor
	 * 
	 * @param StorageItem $item      storage item
	 * @param string      $requestId request-id
	 */
	public function __construct(RenderSource $item, $requestId)
	{
		// Simply calls the parent constructor.
		parent::__construct($item, $requestId);
	}
	
	/**
	 * Render item
	 *
	 * @throws HttpException
	 * @throws Exception
	 *
	 * @return none
	 */
	public function render()
	{
		parent::render();
		$this->item->renderContent();
	}
}

}