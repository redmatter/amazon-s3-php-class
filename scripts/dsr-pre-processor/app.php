#!/usr/bin/php54
<?php
/**
 * DSR Preprocessor
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

define('APP_NAME_', 'rm-archdsrpreproc');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', false);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

date_default_timezone_set('Europe/London');

/** @noinspection UntrustedInclusionInspection */
require_once 'rmapp/class.BaseApp.php';

define('ARCHIVING_INC_DIR', is_dir(__DIR__.'/../inc')? (__DIR__.'/../inc'): 'archiving');
if (file_exists(__DIR__.'/autoload.php'))
{
    // for dev setup where autoload.php exists in the same directory as app.php
    include_once __DIR__.'/autoload.php';
}
else
{
    // for packaged setup where autoload.php is copied to /usr/share/php/archiving
    /** @noinspection PhpIncludeInspection */
    include_once ARCHIVING_INC_DIR.'/archdsrpreproc_autoload.php';
}

use \RedMatter\Archiving\DataSubjectRequest\App;

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
$app [options]

Data-subject requests pre-processor

Delete DSR:
 - Find and add any associated file-types
 - Auto-approval

Options:

    --no-daemon
    
        Run just once in the foreground
        
    --conf <path to config file>
    
        Specify a different config
        
    --help

USAGE;
}

try
{
    CLI::Init(null, [ 'conf:', 'help', 'no-daemon' ]);

    if (true === CLI::Get('help', false))
    {
        Usage();
        exit(0);
    }

    $app = App::getInstance(CLI::Get('conf', App::CONFIG_PATH));
    $conf = $app->getConf();

    if (false === CLI::Get('--no-daemon', false))
    {
        $signalHandlers = array();
        $signalHandlers[SIGTERM] = $signalHandlers[SIGINT] = function ($signo)
        {
            LogVerbose("Received SIG:$signo - Terminating");
            DaemonScripts::Terminate($signo);
        };

        DaemonScripts::Initialise(
            array(
                'runasUid' => $conf->get('app.run_as_user', 'cdrmunch'),
                'runasGid' => $conf->get('app.run_as_group', 'cdrmunch'),
                'pidfilePath' => $conf->get('app.pid_file', '/var/run/archiving/rmarchdsrpreproc.pid'),
                'signalHandlers' => $signalHandlers,
                'sleepInterval' => array(5 /*seconds and*/, 0 /* micro second */),
                'runnableFunction' => array($app, 'run')
            )
        );
    }
    else
    {
        // run once!!
        exit($app->run());
    }
}
/** @noinspection PhpRedundantCatchClauseInspection This can be thrown from anywhere CLI args are accessed */
catch(CLIUsageErrorException $e)
{
    Usage($e->getMessage());
    exit(1);
}
catch (Exception $e)
{
    LogError((string)$e);
    exit(1);
}
