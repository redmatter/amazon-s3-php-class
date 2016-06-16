<?php

/**
 * DateRange.php
 *
 * PHP version 5
 *
 * @category  DateRange.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv\Helpers\Adaptors;

use RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset\Selection\Criterion\ValueRange;

/**
 * Class DateRange
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv\Helpers\Adaptors
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class DateRange
{
    public $Min;
    public $Max;

    /**
     * DateRange constructor.
     *
     * @param \DateTime|null $Min min
     * @param \DateTime|null $Max max
     */
    public function __construct(\DateTime $Min = null, \DateTime $Max = null)
    {
        $this->Min = $Min;
        $this->Max = $Max;
    }

    /**
     * Convert to array
     *
     * @param ValueRange $range the date range
     *
     * @return self
     */
    public static function fromValueRange(ValueRange $range)
    {
        return self::fromUtcDateStrings($range->Min, $range->Max);
    }

    /**
     * From string components
     *
     * @param string|null $min min
     * @param string|null $max max
     *
     * @return DateRange
     */
    public static function fromUtcDateStrings($min = null, $max = null)
    {
        return new self(
            $min ? self::getUtcDateTimeFromString($min) : null,
            $max ? self::getUtcDateTimeFromString($max) : null
        );
    }

    /**
     * Get date converted to UTC
     *
     * @param string $date date to convert to UTC
     *
     * @return \DateTime
     */
    public static function getUtcDateTimeFromString($date)
    {
        return new \DateTime($date, new \DateTimeZone('UTC'));
    }

    /**
     * To string
     *
     * @return string
     */
    public function __toString()
    {
        return sprintf(
            '{Min: %s Max: %s}',
            $this->Min ? $this->Min->format('Y-m-d'): 'NULL',
            $this->Min ? $this->Max->format('Y-m-d'): 'NULL'
        );
    }
}
