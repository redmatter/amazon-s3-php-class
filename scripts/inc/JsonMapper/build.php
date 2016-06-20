<?php

/**
 * Build script
 *
 * PHP version 5
 *
 * @category  Build
 * @package   Build
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

require __DIR__.'/vendor/mtdowling/burgomaster/src/Burgomaster.php';

// Creating staging directory at guzzlehttp/src/build/artifacts/staging.
$stageDirectory = __DIR__ . '/artifacts/staging';
// The root of the project is up one directory from the current directory.
$projectRoot = __DIR__;
$packager = new \Burgomaster($stageDirectory, $projectRoot);

// Copy basic files to the stage directory. Note that we have chdir'd onto
// the $projectRoot directory, so use relative paths.
// foreach (['README.md', 'LICENSE'] as $file) {
    // $packager->deepCopy($file, $file);
// }

// Copy each dependency to the staging directory. Copy *.php and *.pem files.
$packager->recursiveCopy('vendor/netresearch/jsonmapper/src/', '', ['php']);
// Create the classmap autoloader, and instruct the autoloader to
$packager->createAutoloader(
    array(
        'JsonMapper_Exception' => 'JsonMapper/Exception.php',
        'JsonMapper' => 'JsonMapper.php',
    )
);
// Create a phar file from the staging directory at a specific location
$packager->createPhar(__DIR__ . '/artifacts/JsonMapper.phar');
