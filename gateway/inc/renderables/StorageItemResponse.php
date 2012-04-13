<?php

namespace rmarchgateway\renderables
{

use rmarchgateway\StorageItem;
use fluid\Renderable;

class StorageItemResponse implements Renderable
{
	protected $item;
	protected $requestId;
	protected $withBody;
	protected $withMetadata;
	
	public function __construct(StorageItem $item, $requestId, $withBody, $withMetadata)
	{
		$this->item = $item;
		$this->requestId = $requestId;
		$this->withBody = $withBody;
		$this->withMetadata = $withMetadata;
	}
	
	public function render()
	{
		$this->item->Render($this->requestId, $this->withBody, $this->withMetadata);
	}
}

}