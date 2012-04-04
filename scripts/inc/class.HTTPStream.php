<?php

//NoSniff

/**
 * HTTPStream
 *
 * PHP version 5
 *
 * @category  Files
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
 * HTTPStream
 *
 * @category  Library
 * @package   RedMatter_Response
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class HTTPStream
{
	private $_url;
	private $_context;

	private $_httpCode = null;
	private $_httpContentLength = null;

	protected $filter = null;
	protected $fp = null;
	
	/**
	 * Ctor
	 * 
	 * @param string $method  HTTP Method
	 * @param string $url     full URL
	 * @param string $body    body / null
	 * @param array  $headers headers / null
	 */
	protected function __construct($method, $url, $body=null, $headers=null)
	{
		$this->_url = $url;

		if (isset($body) && !isset($headers['Content-Type']))
			$headers['Content-Type'] = 'application/xml';

		$context = array(
			'http' => array(
				'method' => $method,
				'header' => isset($headers)? array_reduce(array_keys($headers), function ($value, $key) use ($headers) { return "{$value}{$key}: {$headers[$key]}\r\n"; }, "") : null,
				'content'=> $body,
				'ignore_errors'=>true
			)
		);

		LogDebug($this->_url, 'URL');
		LogDebug($context, 'context');
		$this->_context = stream_context_create($context);
	}

	/**
	 * Initialize HTTPStream
	 * 
	 * @throws Exception
	 *
	 * @return HTTPStream
	 */
	private function _Initialise()
	{
		$this->fp = @fopen($this->_url, 'rb', false, $this->_context);
		if (false === $this->fp)
		{
			LogError('Failed opening stream to ArchiveD');
			throw new Exception("Not found", 404);
		}

		$meta = @stream_get_meta_data($this->fp);
		if (isset($meta) && isset($meta['wrapper_data']))
			$this->headers = $meta['wrapper_data'];

		if (!isset($this->headers))
			throw new Exception('Unexpected stream state', 500);

		$matches = null;
		foreach ($this->headers as $hdr)
		{
			if (!isset($this->_httpCode))
			{
				$ret = false;
				$ret = preg_match('/^HTTP\\/([0-9].[0-9]) ([0-9]+) (.*)$/', $hdr, $matches);
				if ($ret === false)
					throw new Exception("Regex failure");
				elseif ($ret == 1)
				{
					LogDebug($matches[1], 'HTTP Version');

					LogDebug($matches[2], 'HTTP Status');
					$this->_httpCode = $matches[2];

					LogDebug($matches[3], 'HTTP Message');
					$this->_httpMessage = $matches[3];

					continue;
				}
			}

			if (!isset($this->_httpContentLength))
			{
				$ret = false;
				$ret = preg_match('/^Content-Length: ([0-9]+)$/', $hdr, $matches);
				if ($ret === false)
					throw new Exception("Regex failure");
				elseif ($ret == 1)
				{
					LogDebug($matches[1], 'ContentLength');
					$this->_httpContentLength = $matches[1];

					continue;
				}
			}
		}

		return $this;
	}

	/**
	 * Instantiate and initialise
	 * 
	 * @param string $method  HTTP Method
	 * @param string $url     full URL
	 * @param string $body    body / null
	 * @param array  $headers headers / null
	 * 
	 * @return HTTPStream
	 */
	public static function Make($method, $url, $body=null, $headers=null)
	{
		$ret = new self($method, $url, $body, $headers);
		return $ret->_Initialise();
	}

	/**
	 * Destructor
	 */
	public function __destruct()
	{
		if(is_resource($this->fp))
		{
			fclose($this->fp);
			$this->fp = null;
		}
	}

	/**
	 * Magic function
	 * 
	 * @param property $name name
	 * 
	 * @return mixed
	 */
	public function __get($name)
	{
		switch ($name)
		{
			case 'httpCode':
				return $this->_httpCode;
			
			case 'contentLength':
				return $this->_httpContentLength;
		}
		
		return null;
	}
	
	/**
	 * Fetch body
	 *
	 * @return string
	 */
	public function getContents()
	{
		return stream_get_contents($this->fp);
	}
	
	/**
	 * Spit contents to a file/file-handle/output
	 *
	 * @param mixed $out file/file-handle
	 *
	 * @return int number of bytes copied
	 */
	public function spitContents($out=null)
	{
		if (!is_null($out))
		{
			if (!is_resource($out))
				$out = fopen($out, 'wb');
		}
		else
		{
			$out = fopen('php://output', 'wb');
		}
	
		if ($this->filter)
			stream_filter_append($this->fp, $this->filter);
	
		return stream_copy_to_stream($this->fp, $out);
	}
	
	/**
	 * Setup filter
	 *
	 * @param string $filter filter
	 *
	 * @return none
	 */
	public function setContentFilter($filter)
	{
		$this->filter = $filter;
	}
	
}
