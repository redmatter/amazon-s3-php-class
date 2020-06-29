<?php
/**
 * BulkDeleteRequesterApp.php
 *
 * PHP version 5
 *
 * @category  BulkDeleteRequesterApp.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2020 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\Apps;

use API;
use ArrayStorage;
use BaseApp;
use CLI;
use CLIUsageErrorException;
use EventLog;
use Exception;
use HAPI;
use MySQLiBindParams as Params;
use MySQLiResult_Row as Row;
use MySQLiTransactional as DB;
use RedMatter\Services\CoreAPI;
use RedMatter\Services\DGAPI;
use RuntimeException;

class BulkDeleteRequesterApp extends BaseApp
{
    const CONFIG_PATH = '/etc/archiving/bulk-delete-requester.conf';

    /** @var DGAPI */
    private $dgapi;

    /**
     * App constructor.
     *
     * @param string     $configPath
     * @param array|null $shortOpts
     * @param array|null $longOpts
     */
    protected function __construct($configPath, array $shortOpts = null, array $longOpts = null)
    {
        parent::__construct($configPath, $shortOpts, $longOpts);

        $config = $this->getConf();

        $storage = new ArrayStorage();
        API::Init($config->get('api.base_url', 'http://api'), $storage, APP_NAME_ . ':1.0');

        $this->dgapi = new DGAPI(
            $config->get('dgapi.base_url', 'http://dgapi'),
            function () {
                return API::GetToken(true)->Token;
            }
        );
    }

    public function __destruct()
    {
        API::LogOut();
    }

    /**
     * Get password from interactive shell.
     *
     * @param string $prompt
     *
     * @return string
     * @throws Exception
     */
    public function getPassword($prompt = 'Enter Password:')
    {
        $password = CLI::Get('password', false);
        if ($password !== false) {
            return $password;
        }

        if (stripos(PHP_OS, 'win') === 0) {
            throw new RuntimeException('Windows is not supported non-echo prompt');
        }

        if (rtrim(shell_exec("/usr/bin/env bash -c 'echo OK'")) !== 'OK') {
            throw new RuntimeException("Could not execute bash");
        }

        $command = "/usr/bin/env bash -c 'read -s -p \"" . addslashes($prompt) . "\" v && echo \$v'";

        return rtrim(shell_exec($command));
    }

    /**
     * @inheritDoc
     * @throws CLIUsageErrorException
     * @throws Exception
     */
    public function run()
    {
        $orgId = CLI::Get('org-id');
        $externalReference = CLI::Get('external-ref');
        $reason = CLI::Get('reason');

        $username = CLI::Get('username');
        $password = $this->getPassword();
        $passcode = CLI::Get('2fa-code', null, true, false);

        $sql = CLI::Get('sql', false);
        $sqlFile = CLI::Get('sql-file', false);
        if ($sql === false && $sqlFile === false) {
            throw new CLIUsageErrorException("One of '--sql' or '--sql-file' must be specified");
        }

        if ($sql !== false && $sqlFile !== false) {
            throw new CLIUsageErrorException("Only one of '--sql' and '--sql-file' can be specified");
        }

        if ($sqlFile !== false) {
            if (!is_readable($sqlFile)) {
                throw new CLIUsageErrorException("Cannot read from sql-file");
            }

            $sql = file_get_contents($sqlFile);
        }

        try {
            API::LoginUsernamePassword($username, $password, $passcode);

            $ev = CoreAPI::CreateEventLog(
                EventLog::LEVEL_MESSAGE,
                $orgId,
                'Archiving', 'DeleteRequest:Created',
                'Archive Delete Request - Created',
                0,
                0,
                (object)array(
                    'Reason'      => $reason,
                    'AnyAllowed'  => 'YES',
                    'BulkRequest' => 'YES',
                    'ExternalRef' => $externalReference
                )
            );

            $time = time();

            $me = CoreAPI::GetUserDetails('me');

            $db = DB::Get('retention');
            $db->StartTransaction();
            $db->query(
                "INSERT INTO `ArchivingRetention`.`DeleteRequest_{$orgId}`" .
                " ( `Time`, `OrgID`, `UserID`, `AuditLogID`, `State`, `ExternalReference` )" .
                " VALUES ( ? , ? , ? , ? , 'Pending' , ? )",
                Params::Create()
                    ->add('i', $time)
                    ->add('i', $orgId)
                    ->add('i', $me->ID)
                    ->add('i', $ev->id)
                    ->add('s', $externalReference)
            );
            $deleteRequestId = $db->insert_id;

            $recordsFound = false;
            DB::Get('retention', true)
                ->query($sql, MYSQLI_USE_RESULT)
                ->for_each_batch(
                    /**
                     * @param Row[] $batch
                     * @param int   $flag
                     */
                    function ($batch, $flag) use ($orgId, $deleteRequestId, &$recordsFound, &$db) {
                        $recordsFound = true;
                        $params = Params::Create();
                        $sql = "INSERT INTO `ArchivingRetention`.`DeleteRequestItems_{$orgId}`" .
                            " (`DeleteRequestID`, `OrgID`, `RetentionID`)" .
                            " VALUES ";
                        $first = true;
                        foreach ($batch as $row) {
                            if (!$first) {
                                $sql .= ',';
                            }

                            $sql .= '( ? , ? , ? )';
                            $params
                                ->add('i', $deleteRequestId)
                                ->add('i', $orgId)
                                ->add('i', $row->ID);

                            $first = false;
                        }

                        $db->query($sql, $params);
                    }, 2000);

            if ($recordsFound === false) {
                throw new RuntimeException("No records were found for the given SQL");
            }

            $db->CommitTransaction();

            $this->sendEmail($orgId, $externalReference, $reason, $sql, $deleteRequestId);

            return 0;
        } catch (Exception $e) {
            $this->sendEmail($orgId, $externalReference, $reason, $sql, null, $e->getMessage());
            throw $e;
        }
    }

    /**
     * @param int         $orgId
     * @param string      $externalReference
     * @param string      $reason
     * @param string      $sql
     * @param int         $requestId
     * @param string|null $error
     *
     * @throws Exception
     */
    private function sendEmail($orgId, $externalReference, $reason, $sql, $requestId = null, $error = null)
    {
        $sudoUid = getenv('SUDO_UID');
        $sudoUser = getenv('SUDO_USER');

        /** @noinspection NullPointerExceptionInspection */
        $hostType = HAPI::GetLocalhost()->Get('HostType');
        $hostname = gethostname();

        if ($error === null) {
            $comment = "A bulk delete request has been created.";
            $requestDetail = "DeleteRequestID: {$requestId}\n";
        } else {
            $comment = "Attempt to create a bulk delete request failed.</br>Error: {$error}";
            $requestDetail = '';
        }

        $text = /** @lang HTML */
            <<<HTML
<p>$comment</p>
<p>Details as below.</p>
<pre>
Operator: {$sudoUser} (uid: {$sudoUid})
Org ID: {$orgId}
External Reference: {$externalReference}
Reason: "{$reason}"
{$requestDetail}Selector SQL: {$sql}
Environment: {$hostType} ($hostname)
</pre>
HTML;
        $address = $this->conf->get('notify.email_address');

        $attemptsRemaining = 2;
        while ($attemptsRemaining) {
            $result = $this->dgapi->SendEmail(1, [['root', $address]], 'Bulk Delete Request', $text, '&nbsp;', null, null, true);
            if (true === $result) {
                break;
            }

            usleep(2000000 + mt_rand(-100000, 100000));
            $attemptsRemaining--;
        }
    }
}
