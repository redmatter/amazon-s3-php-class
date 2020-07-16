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
    const INSERT_BATCH_SIZE = 2000;

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
     * Get input from interactive shell without echo.
     *
     * @param string $prompt
     *
     * @return string
     * @throws Exception
     */
    public function getFromPromptWithoutEcho($prompt)
    {
        if (stripos(PHP_OS, 'win') === 0) {
            throw new RuntimeException('Windows is not supported non-echo prompt');
        }

        if (rtrim(shell_exec("/usr/bin/env bash -c 'echo OK'")) !== 'OK') {
            throw new RuntimeException("Could not execute bash");
        }

        $command = "/usr/bin/env bash -c 'read -s -p \"" . addslashes($prompt) . "\" v && echo \$v'";

        $entry = rtrim(shell_exec($command));
        echo "\n"; // move the caret on from the prompt line

        return $entry;
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
        $password = $this->getFromPromptWithoutEcho('Enter Password:');
        $passcode = null;
        if (false === CLI::Get('no-2fa', false)) {
            $passcode = $this->getFromPromptWithoutEcho('Enter Passcode (2FA):');
        }

        $idsFile = CLI::Get('retention-id-file');
        if (!is_readable($idsFile)) {
            throw new CLIUsageErrorException("Cannot read from retention-id-file");
        }

        $ids = file($idsFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
        if ($ids === false) {
            throw new RuntimeException("Cannot read from $idsFile");
        }

        $ids = $this->normaliseIds($ids);
        $idsCount = count($ids);
        if ($idsCount === 0) {
            throw new RuntimeException("No IDs were found for the given file");
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

            $batchCount = ceil($idsCount / self::INSERT_BATCH_SIZE);
            for ($i = 0; $i < $batchCount; $i++) {
                $params = Params::Create();
                $sql = "INSERT INTO `ArchivingRetention`.`DeleteRequestItems_{$orgId}`" .
                    " (`DeleteRequestID`, `OrgID`, `RetentionID`)" .
                    " VALUES ";
                for ($j = 0; $j < self::INSERT_BATCH_SIZE; $j++) {
                    $idx = $i * self::INSERT_BATCH_SIZE + $j;
                    if ($idx >= $idsCount) {
                        break;
                    }
                    if ($j > 0) {
                        $sql .= ',';
                    }

                    $sql .= '( ? , ? , ? )';
                    $params
                        ->add('i', $deleteRequestId)
                        ->add('i', $orgId)
                        ->add('i', $ids[$idx]);
                }

                $db->query($sql, $params);
            }

            $db->CommitTransaction();

            $this->sendEmail($orgId, $externalReference, $reason, $idsCount, $deleteRequestId);

            return 0;
        } catch (Exception $e) {
            $this->sendEmail($orgId, $externalReference, $reason, $idsCount, null, $e->getMessage());
            throw $e;
        }
    }

    /**
     * @param int         $orgId
     * @param string      $externalReference
     * @param string      $reason
     * @param int         $itemsCount
     * @param int         $requestId
     * @param string|null $error
     *
     * @throws Exception
     */
    private function sendEmail($orgId, $externalReference, $reason, $itemsCount, $requestId = null, $error = null)
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
{$requestDetail}Number of IDs : {$itemsCount}
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

    /**
     * @param array $ids
     *
     * @return array
     */
    private function normaliseIds(array $ids)
    {
        // Pack them as INTs and set any non-numeric values will be set to zero.
        // These zeros will be filtered out later on.
        $ids = array_map(function ($v) {
            if (!is_numeric($v)) {
                return 0;
            }

            return (int)$v;
        }, $ids);

        // make sure there are no duplicates
        $ids = array_unique($ids);

        // filter out any zeros or negative numbers
        return array_filter($ids, function ($v) {
            return $v > 0;
        });
    }
}
