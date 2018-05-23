<?php
/**
 * Cache.php
 *
 * PHP version 5
 *
 * @category  Cache.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Cache;

class Cache
{
    /** @var CachedValue[] */
    protected $cache;
    /** @var callable */
    protected $callable;
    /** @var int seconds to keep an item in cache */
    protected $ttlSeconds;

    public function __construct($callable, $ttlSeconds)
    {
        $this->callable = $callable;
        $this->ttlSeconds = $ttlSeconds;
    }

    public function getItem(LookupArgs $args)
    {
        $now = time();
        $key = $args->getKey();
        if (!isset($this->cache[$key]) || $this->cache[$key]->isExpired($now))
        {
            $this->cache[$key] = new CachedValue(call_user_func_array($this->callable, $args->getArgs()), $now + $this->ttlSeconds);
        }

        return $this->cache[$key]->getValue();
    }

    public function prune()
    {
        $now = time();
        $keys = array_keys($this->cache);
        foreach ($keys as $key)
        {
            if ($this->cache[$key]->isExpired($now))
            {
                unset($this->cache[$key]);
            }
        }
    }

    public function __invoke()
    {
        $args = new LookupArgs(func_get_args());
        return $this->getItem($args);
    }
}
