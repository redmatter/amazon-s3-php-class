<?php

namespace rmarchgateway\resources
{
	
use rmarchgateway\renderables\StorageItemResponse;

use rmarchgateway\renderables\PutOK;

use rmarchgateway\StorageItem;

class Storage extends \fluid\Resource
{
	const META_HEADER_PREFIX = 'x-amz-meta-';
	const META_HEADER_PREFIX_LENGTH = 11;
	
	public function HandlePut()
	{
		$metadata = array();
		foreach ($this->client->request->headers as $header => $value)
			if (0 === stripos($header, self::META_HEADER_PREFIX))
				$metadata[substr($header, self::META_HEADER_PREFIX_LENGTH)] = $value;
		
		$given_md5_base64 = isset($this->client->request->headers['Content-MD5'])? $this->client->request->headers['Content-MD5']: null;
				
		$item = new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3], $metadata, $this->client->request->data->as_stream, $given_md5_base64);
		$item->Write();
		$item->Commit();

		return new PutOK($item, $this->client->request->id);
	}
	
	public function HandleGet()
	{
		return new StorageItemResponse(new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id, true, true);
	}
	
	public function HandleHead()
	{
		return new StorageItemResponse(new StorageItem($this->params[0], $this->params[2], $this->params[1], $this->params[3]), $this->client->request->id, false, true);
	}
	
	public static function GetResourceInstance($params, \fluid\Client &$client, $resource_type=null)
	{
		return new self($params, $client);
	}
	
	public function NeedAuth()
	{
		return false;
	}
	
	public function IsAllowedDirectRouting()
	{
		return true;
	}
}
	
}