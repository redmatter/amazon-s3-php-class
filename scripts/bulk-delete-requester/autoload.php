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

define('NS_ARCHIVING_APPS', 'RedMatter\Archiving\Apps');
define('PATH_ARCHIVING_APPS', ARCHIVING_INC_DIR.'/RedMatter/Archiving/Apps');

define('NS_SERVICES', 'RedMatter\Services');
define('PATH_SERVICES', ARCHIVING_INC_DIR.'/RedMatter/Services');

$map = [
    NS_ARCHIVING_APPS.'\BulkDeleteRequesterApp' => PATH_ARCHIVING_APPS.'/BulkDeleteRequesterApp.php',

    NS_SERVICES.'\DGAPI' => PATH_SERVICES.'/DGAPI.php',
    NS_SERVICES.'\CoreAPI' => PATH_SERVICES.'/CoreAPI.php',
];

spl_autoload_register(
    function ($class) use ($map)
    {
        if (array_key_exists($class, $map)) {
            /** @noinspection PhpIncludeInspection */
            include $map[$class];
        }
    },
    true
);

require_once 'rmapp/class.BaseApp.php';
require_once 'eventlog/class.EventLog.php';
