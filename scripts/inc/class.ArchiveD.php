<?php

/**
 * ArchiveD
 *
 * PHP version 5
 *
 * @category PHP
 * @package  PHP__ArchiveD
 * @author   Dino Korah <dino.korah@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 */

/**
 * ConfigMap class that parses the config file, into a memory map
 *
 * PHP version 5
 *
 * @category PHP
 * @package  PHP__ArchiveD
 * @author   Dino Korah <dino.korah@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 */
class ArchiveD
{
	/** @var  ArchiveD */
	private static $_instance;
	
	private $_baseUrl;
	private $_headers;
	private $_authorization;
	
	private $_org;
	private $_user;
	/** @var EventLog  */
	private $_evLog = null;
	
	/**
	 * Ctor
	 * 
	 * @param string $host          host
	 * @param string $authorization auth 
	 * @param string $coreapi       coreapi url
	 * @param string $caseRef       case ref for extract request
	 * @param int    $port          80
	 * @param string $vhost         archived
	 * @param string $protocol      http
	 * 
	 * @throws Exception
	 */
	private function __construct($host, $authorization, $coreapi, $caseRef, $port=null, $vhost='archived', $protocol='http')
	{
		$this->_baseUrl = sprintf('%s://%s%s', $protocol, $host, (!is_null($port)? ":$port": ''));
		$this->_headers = array("Host"=>$vhost);
		$this->_authorization = $authorization;
		
		
		$stream = HTTPStream::Make('GET', "{$coreapi}/users/me", null, array('Authorization'=>$authorization));
		if (!$stream || $stream->httpCode != 200)
			throw new Exception('Could not validate API auth-key');

		$response = new XPath($stream->getContents());
		
		$this->_org = $response('/Users/User/@HomeOrgID');
		$this->_user = $response('/Users/User/@ID');
		
		unset($stream);
		
		if (!$caseRef)
			throw new Exception('No case refrence given');
		
		if (false === ($this->_evLog = EventLog::Message($this->_org, 'Archiving', 'BulkExtract', "Bulk extraction performed under case-ref:{$caseRef}", $this->_user, null, null, $authorization)))
		{
			$this->_evLog = null;
			throw new Exception('Failed to create Audit Log; please see log for details');
		}

		LogMessage("Event log ID for the extraction is {$this->_evLog->id}");
	}
	
	/**
	 * Init
	 * 
	 * @param ConfigMap $config        config
	 * @param string    $authorization auth
	 * @param string    $caseRef       case ref
	 *
	 * @return void
	 */
	public static function Init(ConfigMap $config, $authorization, $caseRef)
	{
		$coreapi = $config->get('coreapi.url', 'http://cdr.coreapi.redmatter.com');
		EventLog::Configure($coreapi);
		
		self::$_instance = new self(
			$config->get('archived.host', 'archived.redmatter.com'),
			$authorization,
			$coreapi,
			$caseRef,
			$config->get('archived.port', null), 
			$config->get('archived.vhost', 'archived'), 
			('false' == $config->get('archived.use_https', 'false'))? 'http': 'https'
		);
	}
	
	/**
	 * Fetch
	 * 
	 * @param string $fileType type
	 * @param string $uuid     uuid
	 *
	 * @return HTTPStream
	 */
	private function _fetch($fileType, $uuid)
	{
		$itemUrl = sprintf('%s/%s/%d/%s?authorization=%s&audit-token=%d', $this->_baseUrl, $fileType, $this->_org, $uuid, $this->_authorization, $this->_evLog->id);
		try
		{
			$stream = HTTPStream::Make('GET', $itemUrl, null, $this->_headers);
			if ($stream->httpCode != 200 || !$stream->contentLength)
			{
				LogWarning($stream->getContents(), "[httpCode:{$stream->httpCode} contentLength:{$stream->contentLength}]");
				throw new Exception("Not found / error [httpCode:{$stream->httpCode} contentLength:{$stream->contentLength}]");
			}
			
			return $stream;
		}
		catch (Exception $e)
		{
			LogWarning($e, "{$fileType}/{$this->_org}/{$uuid}");
		}
		
		return null;
	}
	
	/**
	 * Head
	 * 
	 * @param string $fileType recording / sms
	 * @param string $uuid     uuid
	 * 
	 * @return HTTPStream
	 */
	private function _head($fileType, $uuid)
	{
		$itemUrl = sprintf('%s/%s/%d/%s?authorization=%s&audit-token=%d', $this->_baseUrl, $fileType, $this->_org, $uuid, $this->_authorization, $this->_evLog->id);
		try
		{
			$stream = HTTPStream::Make('HEAD', $itemUrl);
			if ($stream->httpCode != 200 || !$stream->contentLength)
			{
				LogWarning($stream->getContents(), "[httpCode:{$stream->httpCode} contentLength:{$stream->contentLength}]");
				throw new Exception("Not found / error [httpCode:{$stream->httpCode} contentLength:{$stream->contentLength}]");
			}
			
			return $stream;
		}
		catch (Exception $e)
		{
			LogWarning($e, "{$fileType}/{$this->_org}/{$uuid}");
		}
	
		return null;
	}
	
	/**
	 * Fetch
	 * 
	 * @param string $fileType type
	 * @param string $uuid     uuid
	 *
	 * @return HTTPStream
	 */
	public static function Fetch($fileType, $uuid)
	{
		return self::$_instance->_fetch($fileType, $uuid);
	}
	
	/**
	 * Head
	 * 
	 * @param string $fileType recording / sms
	 * @param string $uuid     uuid
	 * 
	 * @return HTTPStream
	 */
	public static function Head($fileType, $uuid)
	{
		return self::$_instance->_head($fileType, $uuid);
	}
}
