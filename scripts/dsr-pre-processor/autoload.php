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

define('NS_DATA_SUBJECT_REQUEST', 'RedMatter\Archiving\DataSubjectRequest');
define('PATH_DATA_SUBJECT_REQUEST', ARCHIVING_INC_DIR.'/RedMatter/Archiving/DataSubjectRequest');
define('NS_CACHE', 'RedMatter\Cache');
define('PATH_CACHE', ARCHIVING_INC_DIR.'/RedMatter/Cache');
define('NS_SERVICES', 'RedMatter\Services');
define('PATH_SERVICES', ARCHIVING_INC_DIR.'/RedMatter/Services');

$map = [
    NS_DATA_SUBJECT_REQUEST.'\App' => PATH_DATA_SUBJECT_REQUEST.'/App.php',
    NS_DATA_SUBJECT_REQUEST.'\AutoApprovedPolicies' => PATH_DATA_SUBJECT_REQUEST.'/AutoApprovedPolicies.php',
    NS_DATA_SUBJECT_REQUEST.'\DsrType' => PATH_DATA_SUBJECT_REQUEST.'/DsrType.php',
    NS_DATA_SUBJECT_REQUEST.'\Notification' => PATH_DATA_SUBJECT_REQUEST.'/Notification.php',

    NS_CACHE.'\Cache' => PATH_CACHE.'/Cache.php',
    NS_CACHE.'\CachedValue' => PATH_CACHE.'/CachedValue.php',
    NS_CACHE.'\LookupArgs' => PATH_CACHE.'/LookupArgs.php',

    NS_SERVICES.'\DGAPI' => PATH_SERVICES.'/DGAPI.php',
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
