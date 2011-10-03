<?php
/**
* Archiving Retention
*
* PHP version 5
*
* @category  MySQLiConnection
* @package   RedMatter_API
* @author    Dino Korah <dino.korah@redmatter.com>
* @copyright 2009-2010 Red Matter Ltd (UK)
* @license   http://www.redmatter.com/license private
* @version   SVN: <svn_id>
* @link      http://www.redmatter.com
*/

/**
 * MySQLiConnection
 *
 * @category  MySQLiConnection
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class MySQLiConnection extends MySQLi
{
	private static $_connections;
	/**
	 * Getter
	 * 
	 * @param mixed $mysql_conf conf
	 * @param bool  $make_new   new?
	 * 
	 * @return MySQLiConnection
	 */
	public static function Get($mysql_conf, $make_new=false)
	{
		if (is_array($mysql_conf) || (true === $make_new) || !isset(self::$_connections[$mysql_conf]))
		{
			$conn = new MySQLiConnection($mysql_conf);
			
			if (!$make_new && !is_array($mysql_conf))
				self::$_connections[$mysql_conf] = $conn;
			
			return $conn;
		}
		else
		{
			$conn = self::$_connections[$mysql_conf];
		
			if ($conn->_ping_interval_seconds && ((time() - $conn->_ping_time) > $conn->_ping_interval_seconds))
			{
				if (true !== $conn->ping())
				{
					self::$_connections[$mysql_conf] = null;
					return MySQLiConnection::Get($mysql_conf, $make_new);
				}
			}
		}
				
		return $conn;
	}
	
	/**
	 * Ping
	 * 
	 * @return bool
	 */
	public function ping()
	{
		$ret = parent::ping();
		$this->log_context->LogDebug('Ping '.($ret? 'OK': 'FAILED'));
		return $ret;
	}
	
	private $_ping_time = 0;
	private $_ping_interval_seconds = 0;
	
	protected $log_context;
	
	/**
	 * Ctor
	 * 
	 * @param mixed $mysql_conf conf
	 * 
	 * @throws ConfigurationErrorException
	 * @throws MySQLiConnectionFailureException
	 * @throws Exception
	 */
	protected function __construct($mysql_conf)
	{
		parent::__construct();
		parent::init();
		
		if (is_array($mysql_conf))
		{
			if (!isset($this->log_context))
				$this->log_context = new SysLogContext('[DB:anonymous]');
			
			$persistent = isset($mysql_conf['persistent'])? ($mysql_conf['persistent'] === true): false;
			$host = isset($mysql_conf['host'])? $mysql_conf['host']: 'localhost';
			$host = $persistent? "p:$host": $host;
			
			if (!isset($mysql_conf['username']))
				throw new ConfigurationErrorException('MySQL configuration doesnt specify username');
			$username = $mysql_conf['username'];
			
			$password = isset($mysql_conf['password'])? $mysql_conf['password']: null;
			$database = isset($mysql_conf['database'])? $mysql_conf['database']: '';
			$port = isset($mysql_conf['port'])? $mysql_conf['port']: null;
			$socket = isset($mysql_conf['socket'])? $mysql_conf['socket']: null;
			
			$flags = 0;
			if (isset($mysql_conf['ssl_enabled']) && ($mysql_conf['ssl_enabled'] === true) && defined('MYSQLI_CLIENT_SSL'))
				$flags |= MYSQLI_CLIENT_SSL;
			
			if (isset($mysql_conf['compress']) && ($mysql_conf['compress'] === true) && defined('MYSQLI_CLIENT_COMPRESS'))
				$flags |= MYSQLI_CLIENT_COMPRESS;
			
			$this->_ping_interval_seconds = isset($mysql_conf['ping_interval_seconds'])? $mysql_conf['ping_interval_seconds']: 0;
		}
		else
		{
			if (!isset($this->log_context))
				$this->log_context = new SysLogContext("[DB:$mysql_conf]");
			
			$prefix = 'mysql'.( ($mysql_conf !== 'default')? "_$mysql_conf": '' );
				
			if (false === config::get("{$prefix}.username", false))
				throw new ConfigurationErrorException("No ".($mysql_conf === 'default'? '': "$mysql_conf ")."database configured");

			$flags = 0;

			if (("true" === config::get("{$prefix}.compress", "false")) && defined('MYSQLI_CLIENT_COMPRESS'))
				$flags |= MYSQLI_CLIENT_COMPRESS;
				
			if ("true" === config::get("{$prefix}.ssl_enabled", "false"))
			{
				if (defined('MYSQLI_CLIENT_SSL'))
					$flags |= MYSQLI_CLIENT_SSL;

				parent::ssl_set(
					config::get("{$prefix}.ssl_key", null),
					config::get("{$prefix}.ssl_cert", null),
					config::get("{$prefix}.ssl_ca", null),
					config::get("{$prefix}.ssl_capath", null),
					config::get("{$prefix}.ssl_cipher", null)
				);
			}

			$persistent = ('true' === config::get("{$prefix}.persistent", "false"));
			$host = config::get("{$prefix}.host", 'localhost');
			$host = $persistent? "p:$host": $host;
			
			if (false === ($username = config::get("{$prefix}.username", false)))
				throw new ConfigurationErrorException('MySQL configuration doesnt specify username');
			
			$password = config::get("{$prefix}.password", null);
			$database = config::get("{$prefix}.database", '');
			$port = config::get("{$prefix}.port", null);
			$socket = config::get("{$prefix}.socket", null);
			
			$this->_ping_interval_seconds = config::get("{$prefix}.ping_interval_seconds", 10);
		}
		
		if (false === parent::real_connect($host, $username, $password, $database, $port, $socket, $flags))
			throw new MySQLiConnectionFailureException('Failed to connect to database'.('default'===$mysql_conf? '': " [config:{$mysql_conf}]"));
		
		if ($this->connect_errno)
			throw new Exception('Failed to connect to database'.('default'===$mysql_conf? '': " [config:{$mysql_conf}]")." error: {$this->connect_error}");
		
		$this->_ping_time = time();
	}
	
	/**
	 * query
	 * 
	 * @param string $query      query
	 * @param int    $resultmode mode
	 * 
	 * @throws MySQLiErrorException
	 * 
	 * @return res
	 */
	public function query($query, $resultmode = MYSQLI_STORE_RESULT)
	{
		$this->log_context->LogDebug($query, 'SQL');
		$res = parent::query($query, $resultmode);
		
		if ($this->errno)
			throw new MySQLiErrorException($this->error, $this->errno, $this);
		
		$this->_ping_time = time();
		
		return $res;
	}
}