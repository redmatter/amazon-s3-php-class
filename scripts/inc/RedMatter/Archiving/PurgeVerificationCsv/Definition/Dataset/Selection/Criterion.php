<?php

/**
 * Criterion.php
 *
 * PHP version 5
 *
 * @category  Criterion.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset\Selection;

/**
 * Class Criterion
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset\Selection
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Criterion
{
    public $Field;
    public $Exclude;
    /**
     * Matching values as a Range
     *
     * @var Criterion\ValueRange
     */
    public $Range;

    /**
     * Matching values as a set
     *
     * @var array
     */
    public $Values;
}
