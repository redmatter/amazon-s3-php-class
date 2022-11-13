#!/usr/bin/php54
<?php
/**
 * Verification CSV generator
 *
 * PHP version 5
 *
 * @category  Archiving
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

define('APP_NAME_', 'rm-purgeverifycsv');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', false);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

date_default_timezone_set('Europe/London');

include_once __DIR__ . '/../../vendor/autoload.php';

use \RedMatter\Archiving\PurgeVerificationCsv\App;

/**
 * Show usage info
 *
 * @param string|null $message message
 *
 * @return void
 */
function Usage($message = null)
{
    if ($message)
    {
        echo "$message\n";
    }
    $app = basename(__FILE__, '.php');
    echo <<<USAGE
$app [options] --definition=/path/to/definition.json

Creates CSV files named purged_XXX.csv and retained_XXX.csv in the
specified output folder.

Please see /usr/share/archiving/purge-verify-definition.example.json for
an example definition. Once you formulate the file, you can validate it
using the --validate option, which will make sure there are no errors.

Options:
    --conf <path to config file>
    --validate <JSON schema validate the definition>
    --min-creation-date <absolute-minimum for creation date>
    --max-creation-date <absolute-maximum for creation date>
    --output-dir <output directory>
    --help

USAGE;
}

try
{
    CLI::Init(
        null,
        [
            'conf:', 'help',
            'auth-creds:', 'auth-hash:',
            'validate', 'definition:', 'min-creation-date:', 'max-creation-date:', 'output-dir:'
        ]
    );

    if (true === CLI::Get('help', false))
    {
        Usage();
        exit(0);
    }

    $app = App::getInstance(CLI::Get('conf', App::CONFIG_PATH));
    $code = $app->run();
    LogConsole($code ? 'Errors while making CSVs' : 'All CSVs created successfully; done');
    exit($code);
}
catch(CLIUsageErrorException $e)
{
    Usage($e->getMessage());
    exit(1);
}
catch (Exception $e)
{
    LogConsole((string)$e);
    exit(1);
}
