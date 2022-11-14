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

define('APP_NAME_', 'rm-arcblkdelreq');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', false);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);

date_default_timezone_set('Europe/London');

require_once __DIR__ . '/../vendor/autoload.php';

use RedMatter\Archiving\Apps\BulkDeleteRequesterApp as App;
use CLIUsageErrorException as UsageError;

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
    $> mysql -u username --batch --column-names=false ArchivingRetention \
          -e "SELECT `ID` FROM `Retention_1001` WHERE `CreationDate` IN ('2020-01-01','2020-01-02')" > /path/to/ids.txt

    $> sudo $app --org-id 1001 --external-ref CSGDPR01937XA \
          --reason 'as per customer request' \
          --retention-id-file /path/to/ids.txt \
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

    Selection of Items to Delete:
    
    The selection of items to delete is done through a text file containing all IDs of retention
    record entries (`Retention_<OrgID>.ID`) of the items to delete. As you can see from the
    example above, you will need to capture all IDs into a file using one or more SQLs executions.

    --retention-id-file <Path>
        Path to file with all Retention IDs to delete.
        
    Auth details:
    
    When running this script, the operator will need to provide the credentials to login to the
    Management Portal. The credentials need not belong to the organisation which the request is
    being created for. You will be presented with a prompt to type in your password and passcode.
    
    --username <UserName>
        Management portal username.
        
    --no-2fa (optional)
        By default you will be asked to type in a passcode from the two-factor auth device. Use
        this option to turn that off.

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
        'no-2fa',

        'org-id:',
        'external-ref:',
        'reason:',

        'retention-id-file:',

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
