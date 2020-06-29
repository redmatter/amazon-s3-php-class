#!/usr/bin/php54
<?php
/**
 * Bulk delete requester
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

use RedMatter\Archiving\Apps\BulkDeleteRequesterApp as App;
use CLIUsageErrorException as UsageError;

define('APP_NAME_', 'rm-arcblkdelreq');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', false);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

date_default_timezone_set('Europe/London');

define('ARCHIVING_INC_DIR', is_dir(__DIR__.'/../inc')? (__DIR__.'/../inc'): 'archiving');
if (file_exists(__DIR__.'/autoload.php')) {
    // for dev setup where autoload.php exists in the same directory as app.php
    include_once __DIR__.'/autoload.php';
} else {
    // for packaged setup where autoload.php is copied to /usr/share/php/archiving
    /** @noinspection PhpIncludeInspection */
    include_once ARCHIVING_INC_DIR.'/arcblkdelreq_autoload.php';
}

/**
 * Show usage info
 *
 * @param string|null $message message
 *
 * @return void
 */
function Usage($message = null)
{
    $app = basename(__FILE__, '.php');

    if ($message) {
        echo "ERROR: $message\n";
        echo "Try `$app --help` for help and documentation.\n";
        return;
    }

    echo <<<USAGE
$app [options]

Bulk delete requester

NOTE: This utility MUST BE run using `sudo` prefix.

    Example:
    $> sudo $app --org-id 1001 --external-ref CSGDPR01937XA \
          --reason 'as per customer request' \
          --sql "SELECT `ID` FROM `ArchivingRetention`.`Retention_1001` WHERE `CreationDate` IN ('2020-01-01','2020-01-02')" \
          --username us@er.com
          
    Example of wrong invocation:
    $> sudo su -
    #> sudo $app --org-id 1001 --external-ref CSGDPR01937XA ...

Options:

    --org-id <OrgID>
        ID of the organisation for which the delete request is to be created.

    --external-ref  <ExternalRef>
        External reference string to link to issue management or CRM

    --reason <Reason>
       Short description of the reason that the will be visible to the customer.

    --help
        This help.

    SQL Selectors:

    An SQL selector can be specified either using the commandline --sql option as a string or be
    loaded from a file using --sql-file option. In the case of the latter, the file is expected to
    have only one SQL. If there are multiple groups of SELECT, use a UNION to combine them into
    one result set. The SQL should select 'Retention_<OrgID>.ID' field aliased as 'ID'.
    
    Example SQL:
        SELECT r.`ID` ID
         FROM `ArchivingRetention`.`Retention_1` r
          JOIN `Org_1`.`Logs` l ON r.`LegUUID` IN (l.`AUUID`, l.`BUUID`)
         WHERE l.`Time` > '2020-01-01'

    --sql <SQLString>
        SQL to run.

    --sql-file <SQLFile>
        File containing the SQL to run.
        
    Auth details:
    
    When running this script, the operator will need to provide the credentials to login to the
    Management Portal. The credentials need not not belong to the organisation which the request
    is being created for.
    
    --username <UserName>
        Management portal username.
        
    --password <Password> (optional)
        Password for the selected UserName. If this option is not given, an interactive prompt
        will appear.
        
    --2fa-code <TwoFactorAuthCode> (optional)
        Passcode from the two-factor auth device.

USAGE;
}

try {
    $sudoUser = getenv('SUDO_USER');
    $user = getenv('USER');
    if ($user !== 'root') {
        throw new UsageError("Should be run with `root` privileges using `sudo` prefix");
    }

    if ($sudoUser === false) {
        throw new UsageError("Should only be run with `sudo` prefix");
    }

    if ($sudoUser === $user) {
        throw new UsageError("Should only be run with `sudo` prefix, from your own login");
    }

    CLI::Init(null, [
        'conf:',

        'username:',
        'password:',
        '2fa-code:',

        'org-id:',
        'external-ref:',
        'reason:',

        'sql:',
        'sql-file:',

        'help'
    ]);

    if (true === CLI::Get('help', false)) {
        Usage();
        exit(0);
    }

    $app = App::getInstance(CLI::Get('conf', App::CONFIG_PATH));
    $exit_code = $app->run();
    if ($exit_code === 0) {
        LogConsole('Delete request successfully created.', null, true);
    } else {
        LogConsole('Failed to create delete request.', null, true);
    }
    exit();
} catch(UsageError $e) {
    Usage($e->getMessage());
    exit(1);
} catch (Exception $e) {
    LogConsole("Failed to create delete request; error:{$e->getMessage()}", null, true);
    LogError((string)$e);
    exit(1);
}
