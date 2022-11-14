<?php

namespace RedMatter\Services\Helper;

use ConfigMap;
use RedMatter\Services\SnowAPI;

trait SnowTrait
{
    /** @var SnowAPI */
    protected $snow;

    /** @var SnowPool */
    protected $snow_pool;

    /**
     * @return ConfigMap
     */
    abstract function getConf();

    /**
     * @return SnowPool
     */
    public function getSnowPool()
    {
        if ($this->snow_pool === null) {
            $this->snow_pool = $this->snow->getSnowPool();
        }

        return $this->snow_pool;
    }

    /**
     * @return int
     */
    protected function getSnowId()
    {
        if ($this->snow === null) {
            $this->snow = new SnowAPI($this->getConf());
        }

        return $this->getSnowPool()->getOneId();
    }
}