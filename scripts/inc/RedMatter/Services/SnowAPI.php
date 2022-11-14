<?php

namespace RedMatter\Services;

use ConfigMap;
use GuzzleHttp\Client;
use RedMatter\Services\Helper\SnowPool;
use Redmatter\Snow\Generator\Generator;

class SnowAPI
{
    /** @var Generator */
    private $snow;

    /**
     * @param ConfigMap $config
     */
    public function __construct(ConfigMap $config)
    {
        $baseUrl = $config->get('snow_api.base_url');
        if ($baseUrl !== null) {
            throw new \RuntimeException("Snow API base-URL not configured");
        }

        $this->snow = new Generator(new Client([
            'base_url' => [$baseUrl],
            'defaults' => [
                'timeout' => $config->get('snow_api.timeout_seconds', 10),
            ],
        ]));
    }

    /**
     * @param $blockSize
     * @return SnowPool
     */
    public function getSnowPool($blockSize = 500)
    {
        return new SnowPool([$this->snow, 'generate'], $blockSize);
    }
}