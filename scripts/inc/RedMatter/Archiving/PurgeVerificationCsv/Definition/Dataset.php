<?php
/**
 * Dataset.php
 *
 * PHP version 5
 *
 * @category  Dataset.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv\Definition;

/**
 * Class Dataset
 *
 * @category  Library
 * @package   RedMatter\Archiving\PurgeVerificationCsv\Definition
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Dataset
{
    /**
     * Expected outcome of the subset; PURGED or RETAINED
     *
     * @var string
     */
    public $ExpectedOutcome;
    /**
     * Select mode RANDOMISED or ALL
     *
     * @var string
     */
    public $SelectionMode;
    /**
     * Randomisation criterion
     *
     * @var Dataset\Randomisation\Criterion
     */
    public $Randomise;
    /**
     * Selection Criterion; the main component of WHERE
     *
     * @var Dataset\Selection\Criterion[]
     */
    public $Criteria;

    /**
     * Setter for Criteria
     *
     * @param Dataset\Selection\Criterion[] $criteria criteria
     *
     * @return void
     */
    public function setCriteria(array $criteria)
    {
        $this->Criteria = array_map(
            function (Dataset\Selection\Criterion $criterion) {
                // $criterion->Range ... not supported; on your own!
                if ($criterion->Field === 'Type' && $criterion->Values) {
                    $criterion->Values = array_map(
                        function ($value) {
                            return \Archiving::GetFileType($value);
                        },
                        $criterion->Values
                    );
                }

                return $criterion;
            },
            $criteria
        );
    }
}
