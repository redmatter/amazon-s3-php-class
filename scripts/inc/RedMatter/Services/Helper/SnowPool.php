<?php

namespace RedMatter\Services\Helper;

use Redmatter\Snow\Block\Block;

class SnowPool
{
    /** @var callable that returns a Block */
    private $generator;

    /** @var Block */
    private $block;

    /** @var int */
    private $blockSize;

    const MIN_SLEEP_MICROSECONDS = 1000;
    const MAX_SLEEP_MICROSECONDS = 3000;
    const MAX_BACKOFF_SECONDS = 180;

    /**
     * @param int $attempt
     * @return int
     */
    private function getBackoff($attempt)
    {
        try {
            $backoff = max(pow($attempt, 2), self::MAX_BACKOFF_SECONDS) * 1e6 +
                random_int(self::MIN_SLEEP_MICROSECONDS, self::MAX_SLEEP_MICROSECONDS);
        } catch (\Exception $e) {
            LogWarning("Failed to get backoff; {$e->getMessage()}; using fixed backoff of 10s");
            $backoff = 10e6;
        }

        return intval($backoff);
    }

    /**
     * @param callable $generator
     * @param int $blockSize allocation size
     */
    public function __construct($generator, $blockSize)
    {
        $this->generator = $generator;
        $this->blockSize = $blockSize;
    }

    /**
     * @return int
     */
    public function getOneId()
    {
        if ($this->block !== null) {
            $this->block->next();
            if ($this->block->valid()) {
                return $this->block->current();
            }
        }

        $attempt = 0;
        while ($this->block === null || !$this->block->valid()) {
            try {
                $this->block = call_user_func_array($this->generator, [$this->blockSize]);
                if ($this->block->valid()) {
                    break;
                }
            } catch (\Exception $e) {
                LogWarning("Failed to get a snow block; {$e->getMessage()}");
            }

            $backoff = $this->getBackoff($attempt);
            LogMessage("Backing off for {$backoff} microseconds; error getting snow block");
            usleep($backoff);
            $attempt++;
        }

        return $this->block->current();
    }
}