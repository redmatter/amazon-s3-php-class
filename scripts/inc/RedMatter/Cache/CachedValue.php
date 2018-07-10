<?php
/**
 * CachedValue.php
 *
 * PHP version 5
 *
 * @category  CachedValue.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */


namespace RedMatter\Cache;


class CachedValue
{
    private $value;
    private $lifeEndTime;

    public function __construct($value, $lifeEndTime)
    {
        $this->value = $value;
        $this->lifeEndTime = $lifeEndTime;
    }

    /**
     * @return mixed
     */
    public function getValue()
    {
        return $this->value;
    }

    /**
     * Is expired at the given time
     *
     * @param int $now
     *
     * @return bool
     */
    public function isExpired($now)
    {
        return $this->lifeEndTime < $now;
    }
}
