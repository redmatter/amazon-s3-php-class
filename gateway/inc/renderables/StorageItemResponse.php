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
use fluid\RenderSource;

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
	
	/**
	 * Ctor
	 * 
	 * @param StorageItem $item      item
	 * @param string      $requestId request id
	 */
	public function __construct(RenderSource $item, $requestId)
	{
		$this->item = $item;
		$this->requestId = $requestId;
	}
	
	/**
	 * (non-PHPdoc)
	 * @see \fluid\Renderable::render()
	 */
	public function render()
	{
		try
		{
			$http_head = $this->item->getHttpResponseHead();
			$http_response_parameters = $this->item->getHttpResponseParameters();
		}
		catch (\rmarchgateway\renderables\StorageError $e)
		{
			LogDebug($e);
			$e.render();
			return;
		}
		catch (\fluid\HttpException $e)
		{
			LogDebug($e);
				
			if (isset($e->DebugMessage))
			{
				LogDebug($e->DebugMessage, 'HttpException');
			}
				
			Response::FromHttpException($e).render();
			return;
		}
		catch (Exception $e)
		{
			LogWarning($e);
			LogDebug("\n".$e->getTraceAsString());
			StorageError::FromException($e).render();
			return;
		}

		$code = $http_head['code'];
		$message = $http_head['message'];

		header("HTTP/1.0 $code $message");
		header("x-amz-request-id: $this->requestId");

		foreach ($http_response_parameters as $parameter)
		{
			if (!empty($parameter))
			{
				header($parameter);
			}
		}				
	}
}

}