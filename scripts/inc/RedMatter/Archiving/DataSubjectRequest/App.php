<?php
/**
 * App.php
 *
 * PHP version 5
 *
 * @category  App.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\DataSubjectRequest;

use RedMatter\Cache\Cache;
use RedMatter\Services\CoreAPI;
use RedMatter\Services\DGAPI;

/** @noinspection SingletonFactoryPatternViolationInspection */

/**
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class App extends \BaseApp
{
    const CONFIG_PATH = '/etc/archiving/dsr-preproc.conf';
    const DATA_SUBJECT_REQUEST_TIME_LIMIT_DAYS = 30;

    private $_timeOfNextLastLegalHoldCacheRun = 0;
    private $_timeOfNextNotificationRun = 0;

    private $_cachedPolicies;
    private $_cachedUsers;
    private $_nextRunTime = 0;

    /**
     * App constructor.
     *
     * @param            $configPath
     * @param array|null $shortOpts
     * @param array|null $longOpts
     */
    protected function __construct($configPath, array $shortOpts = null, array $longOpts = null)
    {
        parent::__construct($configPath, $shortOpts, $longOpts);

        $this->_cachedPolicies = new Cache(function ($orgId) {
            return AutoApprovedPolicies::Create($orgId);
        }, $this->getConf()->get('cache.auto_approved_policies_ttl_seconds', 600));

        $this->_cachedUsers = new Cache(function ($userId) {
            return CoreAPI::GetUserDetails($userId);
        }, $this->getConf()->get('cache.user_details_ttl_seconds', 600));
    }

    /**
     * List of OrgIDs this instance is dealing with
     *
     * @return int[]
     *
     * @throws \MySQLiErrorException
     */
    private function _getOrgIds()
    {
        $orgIds = [];
        // query information-schema to figure out OrgIDs to work with, partitioned across data-centres
        \MySQLiTransactional::Get('retention')->query(
            'SELECT SUBSTR(TABLE_NAME, 15) OrgID ' .
            'FROM `information_schema`.TABLES ' .
            "WHERE TABLES.TABLE_SCHEMA='ArchivingRetention'" .
            " AND TABLE_NAME LIKE 'DeleteRequest\_%'" .
            " AND TABLE_NAME != 'DeleteRequest_Template' " .
            'HAVING ( OrgID % 47 ) % @@auto_increment_increment + 1 = @@auto_increment_offset'
        )->for_each(function ($orgIdRow) use (&$orgIds) {
            $orgIds[] = (int)$orgIdRow->OrgID;
            return true;
        });

        return $orgIds;
    }

    /**
     * @param $orgId
     *
     * @throws \MySQLiErrorException
     */
    private function _processDeleteRequests($orgId)
    {
        $retentionDb = \MySQLiTransactional::Get('retention');
        $assocTypes = \Archiving::GetFileTypesAssociationMap();

        $requests = $retentionDb->query(
            'SELECT `ID`, `UserID` ' .
            "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
            "WHERE `State` = 'Pending' LIMIT 10"
        )->fetch_all();

        foreach ($requests as $request)
        {
            $requestId = (int)$request->ID;
            $requestUserId = (int)$request->UserID;

            // ------------- add-associated filetypes ------------

            foreach ($assocTypes as $type => $assoc) {
                $assocStr = implode(', ', $assoc);
                $retentionDb->query(
                    "INSERT INTO `ArchivingRetention`.`DeleteRequestItems_{$orgId}` " .
                    ' (`DeleteRequestID`, `OrgID`, `RetentionID`) ' .
                    "SELECT {$requestId}, {$orgId}, r_.`ID`" .
                    ' FROM ' .
                    "`ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                    ' ON i.`RetentionID` = r.`ID` ' .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r_ " .
                    ' ON r.`UUID` = r_.`LegUUID` ' .
                    "WHERE i.`DeleteRequestID` = {$requestId} " .
                    " AND r.`Type` = {$type} " .
                    " AND r_.`Type` IN ($assocStr)"
                );
            }

            // ------------- auto-approve items ------------

            /** @var AutoApprovedPolicies $autoApprovePolicies */
            $autoApprovePolicies = call_user_func($this->_cachedPolicies, $orgId);
            $autoApproveUserPolicies = $autoApprovePolicies->getPoliciesByUserId($requestUserId);

            $newState = 'ReadyForApproval';
            if (!empty($autoApproveUserPolicies))
            {
                $autoApproveUserPoliciesStr = implode(', ', $autoApproveUserPolicies);

                $retentionDb->query(
                    "UPDATE `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                    ' ON i.`RetentionID` = r.`ID` ' .
                    "SET i.`State` = 'Approved' " .
                    "WHERE i.`DeleteRequestID` = {$requestId} " .
                    " AND i.`State` = 'Pending' " .
                    " AND r.`PolicyID` IN ({$autoApproveUserPoliciesStr})"
                );

                $counts = $retentionDb->query(
                    'SELECT COUNT(*) Count ' .
                    "FROM `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    "WHERE i.`State` = 'Pending'" .
                    " AND i.`DeleteRequestID` = {$requestId}"
                )->fetch_all();

                if (!$counts[0]->Count)
                {
                    $newState = 'ReadyForPurge';
                }
            }

            $retentionDb->query(
                "UPDATE `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                "SET `State` = '{$newState}' " .
                "WHERE `ID` = {$requestId}"
            );
        }
    }

    /**
     * @param $orgId
     *
     * @throws \MySQLiErrorException
     */
    private function _processLegalHoldRequests($orgId)
    {
        $retentionDb = \MySQLiTransactional::Get('retention');
        $assocTypes = \Archiving::GetFileTypesAssociationMap();

        $requests = $retentionDb->query(
            'SELECT `ID`, `UserID` ' .
            "FROM `ArchivingRetention`.`LegalHold_{$orgId}` " .
            "WHERE `State` = 'Pending' LIMIT 10"
        )->fetch_all();

        foreach ($requests as $request)
        {
            $requestId = (int)$request->ID;
            $requestUserId = (int)$request->UserID;

            // ------------- add-associated filetypes ------------

            foreach ($assocTypes as $type => $assoc) {
                $assocStr = implode(', ', $assoc);
                $retentionDb->query(
                    "INSERT INTO `ArchivingRetention`.`LegalHoldItems_{$orgId}` " .
                    ' (`LegalHoldID`, `RetentionID`) ' .
                    "SELECT {$requestId}, {$orgId}, r_.`ID`" .
                    ' FROM ' .
                    "`ArchivingRetention`.`LegalHoldIItems_{$orgId}` i " .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                    ' ON i.`RetentionID` = r.`ID` ' .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r_ " .
                    ' ON r.`UUID` = r_.`LegUUID` ' .
                    "WHERE i.`LegalHoldIID` = {$requestId} " .
                    " AND r.`Type` = {$type} " .
                    " AND r_.`Type` IN ($assocStr)"
                );
            }

            // ------------- auto-approve items ------------

            /** @var AutoApprovedPolicies $autoApprovePolicies */
            $autoApprovePolicies = call_user_func($this->_cachedPolicies, $orgId);
            $autoApproveUserPolicies = $autoApprovePolicies->getPoliciesByUserId($requestUserId);

            $newState = 'ReadyForApproval';
            if (!empty($autoApproveUserPolicies))
            {
                $autoApproveUserPoliciesStr = implode(', ', $autoApproveUserPolicies);

                $retentionDb->query(
                    "UPDATE `ArchivingRetention`.`LegalHoldIItems_{$orgId}` i " .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                    ' ON i.`RetentionID` = r.`ID` ' .
                    "SET i.`State` = 'Approved' " .
                    "WHERE i.`LegalHoldIID` = {$requestId} " .
                    " AND i.`State` = 'Pending' " .
                    " AND r.`PolicyID` IN ({$autoApproveUserPoliciesStr})"
                );

                $counts = $retentionDb->query(
                    'SELECT COUNT(*) Count ' .
                    "FROM `ArchivingRetention`.`LegalHoldIItems_{$orgId}` i " .
                    "WHERE i.`State` = 'Pending'" .
                    " AND i.`LegalHoldIID` = {$requestId}"
                )->fetch_all();

                if (!$counts[0]->Count)
                {
                    $newState = 'ReadyForPurge';
                }
            }

            $retentionDb->query(
                "UPDATE `ArchivingRetention`.`LegalHoldI_{$orgId}` " .
                "SET `State` = '{$newState}' " .
                "WHERE `ID` = {$requestId}"
            );
        }
    }

    /**
     * Run what you like!
     * The return value is used as the exit code
     *
     * @return int
     *
     * @throws \RuntimeException
     * @throws \Exception
     */
    public function run()
    {
        $now = time();
        if ($this->_nextRunTime > $now)
        {
            return 0;
        }

        $this->_nextRunTime = $now + 300;

        $this->_cachedPolicies->prune();

        $config = $this->getConf();

        $coreapi = $config->get('api.base_url', 'http://api');
        \API::Init($coreapi, new \ArrayStorage(), APP_NAME_ . ':1.0');

        if ($authCreds = $config->get('api.authorization_creds', false)) {
            \API::LoginBasicAuth($authCreds, null/*mfa*/, true/*re_authenticate*/);
        } else {
            /** @noinspection ExceptionsAnnotatingAndHandlingInspection */
            throw new \RuntimeException('No authorization creds specified in config');
        }

        $api = array($coreapi, \API::GetToken());
        /** @noinspection PhpUnusedLocalVariableInspection */
        /** @noinspection OnlyWritesOnParameterInspection */
        $authorizationSentry = new \ScopeSentry(
            function () use (&$api) {
                unset($api);
                \API::LogOut();
            }
        );

        $dgapi = new DGAPI($config->get('dgapi.base_url', 'http://dgapi'), \API::GetToken(true)->Token);

        $orgIds = $this->_getOrgIds();

        foreach ($orgIds as $orgId) {
            if (\DaemonScripts::DispatchAndSleep() && true === \DaemonScripts::Terminate())
            {
                // Exit if Terminate was flagged
                return 0;
            }

            $this->_processDeleteRequests($orgId);
            $this->_processLegalHoldRequests($orgId);

            $retentionDb = \MySQLiTransactional::Get('retention');

            // ------------- Cache status of legal-hold for individual items ------------

            $now = time();
            if ($this->_timeOfNextLastLegalHoldCacheRun < $now) {
                $internalDb = \MySQLiTransactional::Get('internal');

                $internalDb->StartTransaction();
                $retentionDb->query('SELECT li.`RetentionID` RetentionID, ' .
                    'SUM(li.`LegalHoldID` IN (' .
                    " SELECT `ID` FROM `ArchivingRetention`.`LegalHold_{$orgId}` WHERE `ExpiryDate` > CURDATE()" .
                    ')) LegalHoldCount ' .
                    "FROM `LegalHoldItems_{$orgId}` li " .
                    'GROUP BY li.`RetentionID`'
                )->for_each_batch(function ($rows) use ($orgId, $internalDb) {
                    $sql = 'INSERT INTO `ArchivingInternal`.`LegalHoldCache` ' .
                        '(`RetentionID`, `OrgID`, `LegalHoldCount`)' .
                        ' VALUES ' .
                        implode(', ', array_map(function ($row) use ($orgId) {
                                return "({$row->RetentionID}, {$orgId}, {$row->LegalHoldCount})";
                            }, $rows)
                        ) .
                        ' ON DUPLICATE KEY UPDATE `LegalHoldCount`=VALUES(`LegalHoldCount`)';
                    $internalDb->query($sql);
                }, 1000);
                $internalDb->CommitTransaction();

                $this->_timeOfNextLastLegalHoldCacheRun =
                    $now + $config->get('legal_hold.cache_update_interval_minutes', 10) * 60;
            }

            // ------------- find and notify delete-request ------------

            $now = time();
            if ($this->_timeOfNextNotificationRun < $now) {

                $notificationTypesUnion = Notification::GetTypesSqlUnion(DsrType::DELETE_REQUEST);
                $requests = $retentionDb->query(
                    'SELECT dr.`ID` ID, dr.`UserID` UserID, dr.`State` State, dr.`Time`,' .
                    ' dr.`ExternalReference` ExtRef, ' .
                    ' SUM(i.`State` = "Pending") PendingCount, ' .
                    ' SUM(i.`State` = "Approved") ApprovedCount, ' .
                    ' SUM(i.`State` = "Rejected") RejectedCount ' .
                    "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` dr " .
                    "JOIN `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    ' ON dr.`ID` = i.`DeleteRequestID` ' .
                    'WHERE dr.`State` != "Finalised" ' .
                    'GROUP BY ID'
                )->fetch_all();
                foreach ($requests as $request)
                {
                    /** @var Notification[] $notificationSent */
                    $notificationSent = [];
                    $retentionDb->query(
                        'SELECT c.`Type` Type, UNIX_TIMESTAMP(n.`Time`) Time ' .
                        "FROM ({$notificationTypesUnion}) AS c " .
                        "LEFT JOIN `ArchivingRetention`.`DsrNotification_{$orgId}` n" .
                        ' ON c.`Type` = n.`Type`' .
                        ' AND n.`DsrType` = ' . DsrType::DELETE_REQUEST .
                        " AND n.`ID` = {$request->ID}"
                    )->for_each(function ($row) use ($request, &$notificationSent) {
                        $notificationType = (int)$row->Type;
                        $notification = new Notification($notificationType, $row->Time);
                        $notificationSent[$notificationType] = $notification;
                    });

                    $requestTime = new \DateTime("@{$request->Time}", new \DateTimeZone('UTC'));
                    $daysSinceRequest = abs($requestTime->diff(new \DateTime('now', new \DateTimeZone('UTC')))->days);

                    $custodianText = [];
                    $requesterText = [];

                    if ($daysSinceRequest > (self::DATA_SUBJECT_REQUEST_TIME_LIMIT_DAYS - 7)
                        && $request->State !== 'Done'
                        && !$notificationSent[Notification::TYPE_DELAYED_REQUESTER]->isSent())
                    {
                        $requesterText[Notification::TYPE_DELAYED_REQUESTER] = sprintf(
                            '<font color="#ff0000">Your delete-request might not finish by the %d days statutory deadline; ' .
                            'please contact your administrator.</font>', self::DATA_SUBJECT_REQUEST_TIME_LIMIT_DAYS);
                    }

                    switch ($request->State)
                    {
                        case 'Done':
                            if (!$notificationSent[Notification::TYPE_DONE_REQUESTER]->isSent())
                            {
                                $requesterText[Notification::TYPE_DONE_REQUESTER] =
                                    'Your delete-request has been completed.';
                            }
                            break;

                        case 'ReadyForPurge':
                        /** @noinspection PhpMissingBreakStatementInspection */
                        case 'ReadyForApproval':
                            if ($request->State === 'ReadyForApproval'
                                && (!$notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->isSent()
                                    || $notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->getDaysSince() > 7))
                            {
                                $custodianText[Notification::TYPE_PENDING_CUSTODIAN] =
                                    'The above delete-request is waiting for review.';
                            }

                            if ((!$notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->isSent() && $daysSinceRequest > 7)
                                || $notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->getDaysSince() > 7)
                            {
                                $requesterText[Notification::TYPE_PROGRESS_REQUESTER] =
                                    'Your delete-request is in progress.';
                            }

                        case 'Pending':
                            if (!$notificationSent[Notification::TYPE_CREATED_REQUESTER]->isSent())
                            {
                                $requesterText[Notification::TYPE_CREATED_REQUESTER] =
                                    'Your delete-request has been received.';
                            }

                            if (!$notificationSent[Notification::TYPE_CREATED_CUSTODIAN]->isSent())
                            {
                                $custodianText[Notification::TYPE_CREATED_CUSTODIAN] =
                                    'The above delete-request was received.';
                            }
                            break;

                        default:
                            break;
                    }

                    $requestDesc = sprintf(
                        '%s' .
                        'ID: %d<br>' .
                        'Time: %s<br>' .
                        'Status: %s<br>',
                        $request->ExtRef? "Reference: {$request->ExtRef}<br>": '',
                        $request->ID,
                        $requestTime->format('l, d F y H:i:s T'),
                        ($request->State === 'Done') ? '<font color="#00ff00">DONE</font>' : 'PENDING'
                    );

                    $subject = sprintf('Data Delete Request - %sID:%d', $request->ExtRef? "Ref:{$request->ExtRef} ": '', $request->ID);

                    if ($requesterText)
                    {
                        ksort($requesterText);
                        $text = sprintf(
                            '<p>%s</p>' .
                            '<ul>%s%s</ul>' .
                            '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                            $requestDesc,
                            '<li>'.implode('</li><li>', $requesterText).'</li>',
                            ($request->RejectedCount > 0)? '<li>Some items were rejected during review.</li>': '',
                            $orgId,
                            $request->ID
                        );

                        $userDetails = call_user_func($this->_cachedUsers, $request->UserID);
                        $dgapi->SendEmail($orgId, [[$userDetails->Name, $userDetails->UserName]], $subject, $text, 'Data Delete Request', null, null, true);
                    }

                    $custodianRecipients = [];
                    if ($custodianText)
                    {
                        ksort($custodianText);
                        $text = sprintf(
                            '<p>%s</p>' .
                            '<ul>%s</ul>' .
                            '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                            $requestDesc, '<li>'.implode('</li><li>', $custodianText).'</li>', $orgId, $request->ID
                        );

                        /** @var AutoApprovedPolicies $orgPolicies */
                        $orgPolicies = call_user_func($this->_cachedPolicies, $orgId);

                        $dsrPolicies = array_map(function ($row) {
                            return (int)$row->PolicyID;
                        }, $retentionDb->query(
                            'SELECT DISTINCT r.`PolicyID` PolicyID ' .
                            "FROM `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i" .
                            " JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                            '  ON i.`RetentionID`=r.`ID` ' .
                            "WHERE i.`DeleteRequestID`={$request->ID}"
                        )->fetch_all());

                        $custodianRecipients = $orgPolicies->getCustodians($dsrPolicies);

                        $notificationRecipients = array_map(function ($custodianUserId) {
                            $userDetails = call_user_func($this->_cachedUsers, $custodianUserId);
                            return [$userDetails->Name, $userDetails->UserName];
                        }, $custodianRecipients);

                        $dgapi->SendEmail($orgId, $notificationRecipients, $subject, $text, 'Data Delete Request', null, null, true);
                    }

                    if ($request->State === 'Done')
                    {
                        $retentionDb->query(
                            "UPDATE `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                            "SET `State` = 'Finalised' " .
                            "WHERE `ID` = {$request->ID}"
                        );
                    }

                    if ($custodianText || $requesterText)
                    {
                        $notificationTypesSent = array_unique(array_merge(array_keys($custodianText), array_keys($requesterText)));
                        $sqlValues = implode('), (', array_map(function ($type) use ($request, $retentionDb, $orgId, $custodianRecipients) {
                            $notificationRecipientsStr = $retentionDb->real_escape_string(json_encode(
                                // type > 200, then notification is for requester otherwise custodians
                                ($type > 200) ? [$request->UserID] : $custodianRecipients
                            ));
                            return DsrType::DELETE_REQUEST . ", {$request->ID}, {$type}, '{$notificationRecipientsStr}'";
                        }, $notificationTypesSent));

                        $retentionDb->query(
                            "INSERT INTO `ArchivingRetention`.`DsrNotification_{$orgId}` (DsrType, ID, Type, Data)" .
                            " VALUES ({$sqlValues}) ON DUPLICATE KEY UPDATE `Time`=CURRENT_TIMESTAMP()"
                        );
                    }
                }

                $this->_timeOfNextNotificationRun =
                    $now + $config->get('dsr_notifications.check_interval_minutes', 10) * 60;
            }
        }

        return 0;
    }
}
