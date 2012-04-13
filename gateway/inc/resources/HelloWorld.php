<?php

namespace rmarchgateway\resources
{
	
class HelloWorld extends \fluid\Resource
{
	public function HandleGet()
	{
		return new \fluid\renderables\Response('hello world');
	}
	
	public static function GetResourceInstance($params, \fluid\Client &$client, $resource_type=null)
	{
		return new self($params, $client, $resource_type);
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