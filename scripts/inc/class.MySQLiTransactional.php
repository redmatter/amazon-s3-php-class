<?php

/**
 * Transactional Database extension
 *
 * **IMPORTANT**
 * Just a concept; NOT used!!!
 *
 * PHP version 5
 *
 * @category  Libraries
 * @package   RedMatter_ALL
 * @author    Ben Cole <ben.cole@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
 * tXnDatabase - Transactional database class extension
 *
 * @category  Core
 * @package   RedMatter_API
 * @author    Ben Cole <ben.cole@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class MySQLiTransactional extends MySQLiConnection
{
	private static $_transactionals = array();
	private $_origAutocommit;
	private $_transactionDepth;

	/**
	 * PRIVATE constructor; not publically constructable
	 *
	 * @param mixed $config config; see kohana Database docs
	 */
	protected function __construct($config)
	{
		parent::__construct($config);

		$res = $this->query("SELECT @@autocommit `autocommit`");
		if ($row = $res->fetch_object())
			$this->_origAutocommit = $row->autocommit;
		
		$this->_transactionDepth = 0;
		
		if (!isset($this->log_context))
			$this->log_context = new SysLogContext(sprintf("[Transactional DB:%s]", is_array($config)? 'anonymous': $config));
	}

	/**
	 * Factory method to get a connection
	 *
	 * @param mixed $config   config; see kohana Database docs
	 * @param bool  $make_new create a new connection always
	 *
	 * @return MySQLiTransactional
	 */
	public static function GetConnection($config, $make_new = false)
	{
		if (is_string($config) && !$make_new)
		{
			if (!isset(self::$_transactionals[$config]))
				self::$_transactionals[$config] = new MySQLiTransactional($config, $make_new);

			return self::$_transactionals[$config];
		}

		return new MySQLiTransactional($config, $make_new);
	}

	/**
	 * Check if within a transaction
	 *
	 * @return true if yes
	 */
	public function InTransaction()
	{
		return $this->_transactionDepth > 0;
	}

	/**
	 * Start Transaction if there isnt one already in progress
	 *
	 * @return none
	 */
	public function StartTransaction()
	{
		if (0 == $this->_transactionDepth)
		{
			$this->query("START TRANSACTION");
			$this->query("SET autocommit = 0");
			$this->log_context->LogMessage('Start TRANSACTION');
		}

		$this->_transactionDepth++;
	}

	/**
	 * Commit transaction if there is one in progress
	 *
	 * @return none
	 */
	public function CommitTransaction()
	{
		if ($this->_transactionDepth > 0)
		{
			if (1 == $this->_transactionDepth)
			{
				$this->query("COMMIT");
				$this->query("SET autocommit = {$this->_origAutocommit}");
				$this->log_context->LogMessage('End TRANSACTION');
			}

			$this->_transactionDepth--;
		}
	}

	/**
	 * Rollback a transaction if there is one in progress
	 *
	 * @return none
	 */
	public function RollbackTransaction()
	{
		if ($this->_transactionDepth > 0)
		{
			if (!$this->link)
			{
				LogWarning("MySQLiTransactional; link already gone. Could NOT explicitly ROLLBACK");
				return;
			}
			
			$this->query("ROLLBACK");
			$this->query("SET autocommit = {$this->_origAutocommit}");
			$this->_transactionDepth = 0;
			$this->log_context->LogWarning('Rollback TRANSACTION');
		}
	}

	/**
	 * Destructor
	 *
	 * Rolls back any uncommitted transaction
	 */
	public function __destruct()
	{
		$this->RollbackTransaction();
	}
}
