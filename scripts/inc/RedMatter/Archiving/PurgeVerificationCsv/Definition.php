<?php
/**
 * Definition.php
 *
 * PHP version 5
 *
 * @category  Definition.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv;

/**
 * Class Definition
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Definition
{
    public $LagDays;
    /**
     * Csv Selection Criterion
     *
     * @var Definition\Dataset[]
     */
    public $Datasets;
}
