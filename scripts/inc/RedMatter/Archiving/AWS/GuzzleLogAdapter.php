<?php
/**
 * GuzzleLogAdapter.php
 *
 * PHP version 5
 *
 * @category  GuzzleLogAdapter.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2015 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\AWS;

use Guzzle\Log\LogAdapterInterface;

/**
 * Class GuzzleLogAdapter
 *
 * @category  Library
 * @package   RedMatter\Archiving\AWS
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class GuzzleLogAdapter implements LogAdapterInterface
{
    private $prefix;

    /**
     * Constructor
     *
     * @param string $prefix prefix for each line
     */
    public function __construct($prefix = '')
    {
        $this->prefix = $prefix;
    }

    /**
     * Log a message at a priority
     *
     * @param string  $message  Message to log
     * @param integer $priority Priority of message (use the \LOG_* constants of 0 - 7)
     * @param array   $extras   Extra information to log in event
     *
     * @return void
     */
    public function log($message, $priority = LOG_INFO, $extras = array())
    {
        \SysLogger::Log($priority, $this->prefix.$message.(empty($extras)? '' : json_encode($extras)));
    }
}
