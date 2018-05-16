<?php
/**
 * Autoloader
 *
 * PHP version 5
 *
 * @category  Autoloader
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

//define('NS_CSV_DEFINITION', 'RedMatter\Archiving\PurgeVerificationCsv\Definition');
//define('PATH_CSV_DEFINITION', ARCHIVING_INC_DIR.'/RedMatter/Archiving/PurgeVerificationCsv/Definition');
//define('NS_HELPERS', 'RedMatter\Archiving\PurgeVerificationCsv\Helpers');
//define('PATH_HELPERS', ARCHIVING_INC_DIR.'/RedMatter/Archiving/PurgeVerificationCsv/Helpers');

$map = [
    //NS_HELPERS.'\Adaptors\DateRange' => PATH_HELPERS.'/Adaptors/DateRange.php',
    //NS_HELPERS.'\DateRangeIterator' => PATH_HELPERS.'/DateRangeIterator.php',
    //NS_CSV_DEFINITION.'\Dataset\Selection\Criterion' => PATH_CSV_DEFINITION.'/Dataset/Selection/Criterion.php',
    //NS_CSV_DEFINITION.'\Dataset\Selection\Criterion\ValueRange' => PATH_CSV_DEFINITION.'/Dataset/Selection/Criterion/ValueRange.php',
    //NS_CSV_DEFINITION.'\Dataset\Randomisation\Criterion' => PATH_CSV_DEFINITION.'/Dataset/Randomisation/Criterion.php',
    //NS_CSV_DEFINITION.'\Dataset' => PATH_CSV_DEFINITION.'/Dataset.php',
    //NS_CSV_DEFINITION => PATH_CSV_DEFINITION.'.php',

    'RedMatter\Archiving\DataSubjectRequest\App' => ARCHIVING_INC_DIR.'/RedMatter/Archiving/DataSubjectRequest/App.php',
];

spl_autoload_register(
    function ($class) use ($map)
    {
        if (array_key_exists($class, $map))
        {
            /** @noinspection PhpIncludeInspection */
            include $map[$class];
        }
    },
    true
);

//require_once ARCHIVING_INC_DIR.'/JsonSchema-2.0.4.phar';
//require_once ARCHIVING_INC_DIR.'/JsonMapper-0.11.0.phar';
