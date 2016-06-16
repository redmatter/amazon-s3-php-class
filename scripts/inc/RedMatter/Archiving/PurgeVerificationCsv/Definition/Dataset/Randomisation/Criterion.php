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

namespace RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset\Randomisation;

/**
 * Class Criterion
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset\Randomisation
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Criterion
{
    /**
     * Randomise on field
     *
     * @var string
     */
    public $On;
    /**
     * Randomise for each of fields
     *
     * @var string[]
     */
    public $ForEachOf;
}
