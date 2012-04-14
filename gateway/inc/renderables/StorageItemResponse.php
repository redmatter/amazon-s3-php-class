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
use fluid\Renderable;

/**
 * Storage Item response renderer
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class StorageItemResponse implements Renderable
{
	protected $item;
	protected $requestId;
	protected $withBody;
	protected $withMetadata;
	
	/**
	 * Ctor
	 * 
	 * @param StorageItem $item         item
	 * @param sting       $requestId    request id
	 * @param bool        $withBody     with body?
	 * @param bool        $withMetadata with metadata?
	 */
	public function __construct(StorageItem $item, $requestId, $withBody, $withMetadata)
	{
		$this->item = $item;
		$this->requestId = $requestId;
		$this->withBody = $withBody;
		$this->withMetadata = $withMetadata;
	}

	/**
	 * (non-PHPdoc)
	 * @see \fluid\Renderable::render()
	 */
	public function render()
	{
		$this->item->Render($this->requestId, $this->withBody, $this->withMetadata);
	}
}

}