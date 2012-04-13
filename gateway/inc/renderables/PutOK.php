<?php

namespace rmarchgateway\renderables
{
	
use rmarchgateway\StorageItem;

class PutOK extends StorageError
{
	public function __construct(StorageItem $item, $requestId)
	{
		$etag = bin2hex($item->md5);
		parent::__construct(StorageError::PutOK, $item, $requestId, array("ETag: {$etag}"));
	}
}

}