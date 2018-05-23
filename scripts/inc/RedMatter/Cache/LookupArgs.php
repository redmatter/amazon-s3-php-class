<?php
/**
 * LookupArgs.php
 *
 * PHP version 5
 *
 * @category  LookupArgs.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */


namespace RedMatter\Cache;


class LookupArgs
{
    protected $args;
    protected $key = null;

    /**
     * LookupArgs constructor.
     *
     * @throws \InvalidArgumentException if no arguments are given
     */
    public function __construct()
    {
        if (func_num_args() < 1)
        {
            throw new \InvalidArgumentException('At least one argument needed for cache lookup');
        }

        $this->args = func_get_args();
    }

    /**
     * Get a binary-string for usage as array-key
     *
     * @return string
     */
    public function getKey()
    {
        if ($this->key === null)
        {
            $this->key = (count($this->args) === 1 && (is_int($this->args[0]) || is_string($this->args[0]))) ?
                $this->args[0] : sha1(json_encode($this->args), true);
        }

        return $this->key;
    }

    /**
     * @return array
     */
    public function getArgs()
    {
        return $this->args;
    }
}
