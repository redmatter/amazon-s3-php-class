#!/usr/bin/php54
<?php
/**
 * Pcap recording import
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
define('APP_NAME_', 'rm-archpcapimport');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

require_once __DIR__ . '/vendor/autoload.php';

try
{
    CLI::Init(null, array('auth-creds:', 'auth-hash:', 'conf:', 'help', 'no-daemon'));

    if (true === CLI::Get('help', false))
    {
        Usage();
        exit(0);
    }

    $app = 	PcapRecordingImportApp::getInstance(CLI::Get('--conf', PcapRecordingImportApp::CONFIG_PATH));
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
                'pidfilePath' => $conf->get('app.pid_file', '/var/run/archiving/rmarchpcapimport.pid'),
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
catch(CLIUsageErrorException $e)
{
    Usage($e->getMessage());
    exit(1);
}
catch (Exception $e)
{
    LogWarning($e);
    exit(1);
}
