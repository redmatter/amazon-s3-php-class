<?php
/**
 * DateRangeIterator.php
 *
 * PHP version 5
 *
 * @category  DateRangeIterator.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv\Helpers;

use RedMatter\Archiving\PurgeVerificationCsv\Helpers\Adaptors\DateRange;

/**
 * Class DateRangeIterator
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv\Helpers
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class DateRangeIterator implements \Iterator
{
    private $interval;
    /**
     * Current range
     *
     * @var null|DateRange
     */
    private $current = null;

    /**
     * Next min
     *
     * @var \DateTime
     */
    private $nextMin;
    /**
     * Derived ranges
     *
     * @var DateRange[]
     */
    private $ranges;
    private $rangesIndex;

    /**
     * DateRangeIterator constructor.
     *
     * @param DateRange          $range    range
     * @param bool               $exclude  exclude or include
     * @param DateRange          $bounds   absolute bounds
     * @param \DateInterval|null $interval interval
     *
     * @throws \RuntimeException
     */
    public function __construct(DateRange $range, $exclude, DateRange $bounds, \DateInterval $interval = null)
    {
        $this->validateRange($range);
        $this->validateRange($bounds);
        $this->interval = $interval;

        $rangeMin = $range->Min->getTimestamp();
        $rangeMax = $range->Max->getTimestamp();
        $boundsMin = $bounds->Min->getTimestamp();
        $boundsMax = $bounds->Max->getTimestamp();
        if ($rangeMin < $boundsMin || $rangeMin > $boundsMax
            || $rangeMax < $boundsMin || $rangeMax > $boundsMax
        ) {
            throw new \RuntimeException("Given range {$range} does not fit inside bounds {$bounds}");
        }

        // split the ranges here into one or two based on applying the range and absoluteBounds along with exclude
        // iterate over each and use that for range generation, till there is nothing to iterate over

        $this->ranges = [];
        if ($exclude) {
            if ($rangeMin === $boundsMin && $rangeMax === $boundsMax) {
                throw new \RuntimeException('Invalid date range; excludes everuthing');
            }
            if ($boundsMin !== $rangeMin) {
                $this->ranges[] = new DateRange($bounds->Min, $range->Min);
            }
            if ($rangeMax !== $boundsMax) {
                $this->ranges[] = new DateRange($range->Max, $bounds->Max);
            }
        } else {
            $this->ranges[] = $range;
        }
        $this->rangesIndex = 0;
    }

    /**
     * Return the current element
     *
     * @return DateRange Can return any type.
     */
    public function current()
    {
        if ($this->interval) {
            $nextMax = clone $this->nextMin;
            $nextMax->add($this->interval);
            if ($nextMax->getTimestamp() > $this->ranges[$this->rangesIndex]->Max->getTimestamp()) {
                $nextMax = $this->ranges[$this->rangesIndex]->Max;
            }
            $this->current = new DateRange($this->nextMin, $nextMax);
        } else {
            $this->current = $this->ranges[$this->rangesIndex];
        }

        return $this->current;
    }

    /**
     * Move forward to next element
     *
     * @return void Any returned value is ignored.
     * @throws \RuntimeException
     */
    public function next()
    {
        if (!$this->current) {
            throw new \RuntimeException('Invalid state / call-sequence; _current not set');
        }

        if ($this->current->Max->getTimestamp() < $this->ranges[$this->rangesIndex]->Max->getTimestamp()) {
            $this->nextMin = $this->current->Max;
        } elseif ($this->current->Max->getTimestamp() === $this->ranges[$this->rangesIndex]->Max->getTimestamp()) {
            if ($this->rangesIndex < (count($this->ranges) - 1)) {
                $this->nextMin = $this->ranges[++$this->rangesIndex]->Min;
            } else {
                $this->nextMin = $this->current->Max;
            }
        }
    }

    /**
     * Return the key of the current element
     *
     * @return mixed scalar on success, or null on failure.
     */
    public function key()
    {
        return $this->nextMin->getTimestamp();
    }

    /**
     * Checks if current position is valid
     *
     * @return boolean The return value will be casted to boolean and then evaluated.
     *        Returns true on success or false on failure.
     */
    public function valid()
    {
        return $this->nextMin->getTimestamp() < $this->ranges[count($this->ranges) - 1]->Max->getTimestamp();
    }

    /**
     * Rewind the Iterator to the first element
     *
     * @return void Any returned value is ignored.
     */
    public function rewind()
    {
        $this->rangesIndex = 0;
        $this->nextMin = $this->ranges[0]->Min;
    }

    /**
     * Validate date range
     *
     * @param DateRange $range range
     *
     * @throws \RuntimeException if the date range is invalid
     *
     * @return void
     */
    private function validateRange(DateRange $range)
    {
        if (!$range->Min || !$range->Max || $range->Min->getTimestamp() >= $range->Max->getTimestamp()) {
            throw new \RuntimeException(__CLASS__." - Invalid range {$range} given");
        }
    }
}
