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

use rmarchgateway\operations\S3Operation;
	
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
	
	/**
	 * Ctor
	 * 
	 * @param StorageItem $item      item
	 * @param string      $requestId request id
	 */
	public function __construct(S3Operation $item, $requestId)
	{
		$this->item = $item;
		$this->requestId = $requestId;
		
		$this->item->doRequest();
	}
	
	/**
	 * (non-PHPdoc)
	 * @see \fluid\Renderable::render()
	 */
	public function render()
	{
		try
		{
			// Do all the things which could raise an exception before we start outputting HTTP headers or body.
			$http_head = $this->item->getHttpResponseStatus();
			$http_response_parameters = $this->item->getHttpResponseHeaders();
			$body = $this->item->getHttpBody();
			
			if (is_resource($body) === true)
			{
				$out = @fopen('php://output', 'wb');
					
				if ($out === false)
				{
					LogError('Could not open php://output');
					throw new StorageError(StorageError::InternalError);
				}
			}
		}
		catch (\rmarchgateway\renderables\StorageError $e)
		{
			LogWarning($e);
			$e.render();
			return;
		}
		catch (\fluid\HttpException $e)
		{
			LogWarning($e);
				
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

		if (is_resource($body) === true)
		{
			if (stream_copy_to_stream($body, $out) == 0)
			{
				LogWarning('copy_to_stream copied 0 bytes');
			}
			
			@fclose($out);
		}
		else
		{
			echo $body;
		}
	}
}

}