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
    const LEGAL_HOLD_EXPIRY_NOTICE_DAYS = 2;

    private $_timeOfNextLastLegalHoldCacheRun = 0;
    private $_timeOfNextNotificationRun = 0;

    private $_cachedPolicies;
    private $_cachedUsers;
    private $_nextRunTime = 0;

    private $_dgapi;

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

        $config = $this->getConf();

        \API::Init($config->get('api.base_url', 'http://api'), new \ArrayStorage(), APP_NAME_ . ':1.0');

        if ($authCreds = $config->get('api.authorization_creds', false)) {
            try {
                \API::LoginBasicAuth($authCreds, null, true);
            } catch (\Exception $e) {
                LogWarning($e);
            }
        } else {
            /** @noinspection ExceptionsAnnotatingAndHandlingInspection */
            throw new \RuntimeException('No authorization creds specified in config');
        }

        /** @noinspection PhpUnusedLocalVariableInspection */
        /** @noinspection OnlyWritesOnParameterInspection */
        $authorizationSentry = new \ScopeSentry(
            function () {
                \API::LogOut();
            }
        );

        $this->_dgapi = new DGAPI(
            $config->get('dgapi.base_url', 'http://dgapi'),
            function () {
                try
                {
                    return \API::GetToken(true)->Token;
                } catch (\APIAuthRequiredException $e) {
                    LogWarning($e);
                }

                return null;
            }
        );
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
     * Pre-process a delete-request for auto-approval and associated-filetypes
     *
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
            try
            {
                $requestId = (int)$request->ID;
                $requestUserId = (int)$request->UserID;

                $retentionDb->StartTransaction();

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

                $retentionDb->CommitTransaction();
            }
            catch (\Exception $e)
            {
                $retentionDb->RollbackTransaction();
                LogWarning($e);
                continue;
            }
        }
    }

    /**
     * Pre-process a legal-hold for auto-approval and associated-filetypes
     *
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
            try
            {
                $requestId = (int)$request->ID;
                $requestUserId = (int)$request->UserID;

                $retentionDb->StartTransaction();

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

                $retentionDb->CommitTransaction();
            }
            catch (\Exception $e)
            {
                $retentionDb->RollbackTransaction();
                LogWarning($e);
                continue;
            }
        }
    }

    /**
     * Send notifications relating to delete-request and keep track of what has been sent
     *
     * @param int $orgId
     *
     * @throws \MySQLiErrorException
     * @throws \Exception
     */
    private function _sendDeleteRequestNotifications($orgId)
    {
        $notificationTypesUnion = Notification::GetTypesSqlUnion(DsrType::DELETE_REQUEST);

        $retentionDb = \MySQLiTransactional::Get('retention');

        $requestIds = array_map(
            function($row) {
                return $row->ID;
            },
            $retentionDb->query(
                'SELECT `ID` ' .
                "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                "WHERE `State` != 'Finalised'"
            )->fetch_all()
        );

        foreach ($requestIds as $requestId) {
            try
            {
                $requests = $retentionDb->query(
                    'SELECT dr.`ID` ID, dr.`UserID` UserID, dr.`State` State, dr.`Time`,' .
                    ' dr.`ExternalReference` ExtRef, ' .
                    ' SUM(i.`State` = "Pending") PendingCount, ' .
                    ' SUM(i.`State` = "Approved") ApprovedCount, ' .
                    ' SUM(i.`State` = "Rejected") RejectedCount ' .
                    "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` dr " .
                    "JOIN `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    ' ON dr.`ID` = i.`DeleteRequestID`' .
                    "  AND dr.`State` != 'Finalised'" .
                    "  AND dr.`ID` = {$requestId}"
                )->fetch_all();
                $request = $requests[0];

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
                $daysSinceRequest = $requestTime->diff(new \DateTime('now', new \DateTimeZone('UTC')))->days;

                $custodianText = [];
                $requesterText = [];

                if ($daysSinceRequest > (self::DATA_SUBJECT_REQUEST_TIME_LIMIT_DAYS - 7)
                    && $request->State !== 'Done'
                    && !$notificationSent[Notification::TYPE_DELAYED_REQUESTER]->isSent()) {
                    $requesterText[Notification::TYPE_DELAYED_REQUESTER] = sprintf(
                        '<font color="#ff0000">Your delete-request might not finish by the %d days statutory deadline; ' .
                        'please contact your administrator.</font>', self::DATA_SUBJECT_REQUEST_TIME_LIMIT_DAYS);
                }

                switch ($request->State) {
                    case 'Done':
                        if (!$notificationSent[Notification::TYPE_DONE_REQUESTER]->isSent()) {
                            $requesterText[Notification::TYPE_DONE_REQUESTER] =
                                'Your delete-request has been completed.';
                        }
                        break;

                    case 'ReadyForPurge':
                        /** @noinspection PhpMissingBreakStatementInspection */
                    case 'ReadyForApproval':
                        if ($request->State === 'ReadyForApproval'
                            && (!$notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->isSent()
                                || $notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->getDaysSince() > 7)) {
                            $custodianText[Notification::TYPE_PENDING_CUSTODIAN] =
                                'The above delete-request is waiting for review.';
                        }

                        if ((!$notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->isSent() && $daysSinceRequest > 7)
                            || $notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->getDaysSince() > 7) {
                            $requesterText[Notification::TYPE_PROGRESS_REQUESTER] =
                                'Your delete-request is in progress.';
                        }

                    case 'Pending':
                        if (!$notificationSent[Notification::TYPE_CREATED_REQUESTER]->isSent()) {
                            $requesterText[Notification::TYPE_CREATED_REQUESTER] =
                                'Your delete-request has been received.';
                        }

                        if (!$notificationSent[Notification::TYPE_CREATED_CUSTODIAN]->isSent()) {
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
                    $request->ExtRef ? "Reference: {$request->ExtRef}<br>" : '',
                    $request->ID,
                    $requestTime->format('l, d F y H:i:s T'),
                    ($request->State === 'Done') ? '<font color="#00ff00">DONE</font>' : 'PENDING'
                );

                $subject = sprintf('Data Delete Request - %sID:%d',
                    $request->ExtRef ? "Ref:{$request->ExtRef} " : '', $request->ID);

                if ($requesterText) {
                    ksort($requesterText);
                    $text = sprintf(
                        '<p>%s</p>' .
                        '<ul>%s%s</ul>' .
                        '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                        $requestDesc,
                        '<li>' . implode('</li><li>', $requesterText) . '</li>',
                        ($request->RejectedCount > 0) ? '<li>Some items were rejected during review.</li>' : '',
                        $orgId,
                        $request->ID
                    );

                    $userDetails = call_user_func($this->_cachedUsers, $request->UserID);
                    if (true !== $this->_dgapi->SendEmail(
                            $orgId, [[$userDetails->Name, $userDetails->UserName]], $subject, $text,
                            'Data Delete Request', null, null, true)) {
                        // could not send the notification; reset the array which later on gets looked up for "sent"
                        // notification, and saved to db.
                        $requesterText = [];
                    }
                }

                $custodianRecipients = [];
                if ($custodianText) {
                    ksort($custodianText);
                    $text = sprintf(
                        '<p>%s</p>' .
                        '<ul>%s</ul>' .
                        '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                        $requestDesc, '<li>' . implode('</li><li>', $custodianText) . '</li>', $orgId, $request->ID
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

                    // convert custodians user-id list into recipient list
                    // i.e [ui, u2] to [[u1-name, u1-email], [u2-name, u2-email]]
                    $notificationRecipients = array_map(function ($custodianUserId) {
                        $userDetails = call_user_func($this->_cachedUsers, $custodianUserId);

                        return [$userDetails->Name, $userDetails->UserName];
                    }, $custodianRecipients);

                    if (true !== $this->_dgapi->SendEmail(
                            $orgId, $notificationRecipients, $subject, $text,
                            'Data Delete Request', null, null, true))
                    {
                        // reset the flags so that they wouldn't get saved to db which will cause a later retry
                        $custodianText = [];
                    }
                }

                $retentionDb->StartTransaction();

                if ($request->State === 'Done') {
                    $retentionDb->query(
                        "UPDATE `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                        "SET `State` = 'Finalised' " .
                        "WHERE `ID` = {$request->ID}"
                    );
                }

                if ($custodianText || $requesterText) {
                    $notificationTypesSent = array_unique(array_merge(array_keys($custodianText),
                        array_keys($requesterText)));
                    $sqlValues = implode('), (',
                        array_map(function ($type) use ($request, $retentionDb, $orgId, $custodianRecipients) {
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

                $retentionDb->CommitTransaction();
            }
            catch (\Exception $e)
            {
                $retentionDb->RollbackTransaction();
                LogWarning($e);
                continue;
            }
        }
    }


    /**
     * Send notifications relating to legal-hold and keep track of what has been sent
     *
     * @param int $orgId
     *
     * @throws \MySQLiErrorException
     * @throws \Exception
     */
    private function _sendLegalHoldNotifications($orgId)
    {
        $notificationTypesUnion = Notification::GetTypesSqlUnion(DsrType::LEGAL_HOLD);

        $retentionDb = \MySQLiTransactional::Get('retention');

        $requestIds = array_map(
            function($row) {
                return $row->ID;
            },
            $retentionDb->query(
                'SELECT `ID` ' .
                "FROM `ArchivingRetention`.`LegalHold_{$orgId}` " .
                "WHERE `State` != 'Finalised'"
            )->fetch_all()
        );

        foreach ($requestIds as $requestId) {
            try
            {
                $requests = $retentionDb->query(
                    'SELECT lh.`ID` ID, lh.`UserID` UserID, lh.`State` State, lh.`Time`,' .
                    ' lh.`ExternalReference` ExtRef,' .
                    ' UNIX_TIMESTAMP(lh.`ExpiryDate`) ExpiryDate, lh.`ExpiryNotified` ExpiryNotified' .
                    ' SUM(i.`State` = "Pending") PendingCount, ' .
                    ' SUM(i.`State` = "Approved") ApprovedCount, ' .
                    ' SUM(i.`State` = "Rejected") RejectedCount ' .
                    "FROM `ArchivingRetention`.`LegalHold_{$orgId}` lh " .
                    "JOIN `ArchivingRetention`.`LegalHoldItems_{$orgId}` i " .
                    ' ON lh.`ID` = i.`LegalHoldID`' .
                    "  AND lh.`State` != 'Finalised'" .
                    "  AND lh.`ID` = {$requestId}"
                )->fetch_all();
                $request = $requests[0];

                /** @var Notification[] $notificationSent */
                $notificationSent = [];
                $retentionDb->query(
                    'SELECT c.`Type` Type, UNIX_TIMESTAMP(n.`Time`) Time ' .
                    "FROM ({$notificationTypesUnion}) AS c " .
                    "LEFT JOIN `ArchivingRetention`.`DsrNotification_{$orgId}` n" .
                    ' ON c.`Type` = n.`Type`' .
                    ' AND n.`DsrType` = ' . DsrType::LEGAL_HOLD .
                    " AND n.`ID` = {$request->ID}"
                )->for_each(function ($row) use ($request, &$notificationSent) {
                    $notificationType = (int)$row->Type;
                    $notification = new Notification($notificationType, $row->Time);
                    $notificationSent[$notificationType] = $notification;
                });

                $requestTime = new \DateTime("@{$request->Time}", new \DateTimeZone('UTC'));
                $daysSinceRequest = $requestTime->diff(new \DateTime('now', new \DateTimeZone('UTC')))->days;

                $expiryDate = new \DateTime("@{$request->ExpiryDate}", new \DateTimeZone('UTC'));
                $daysToExpiry = $expiryDate->diff(new \DateTime('now', new \DateTimeZone('UTC')))->days * -1;

                $custodianText = [];
                $requesterText = [];

                if ($daysToExpiry < (self::LEGAL_HOLD_EXPIRY_NOTICE_DAYS + 1)
                    && $request->State !== 'Done' && !$request->ExpiryNotified
                    && !$notificationSent[Notification::TYPE_EXPIRY_REQUESTER]->isSent()) {
                    $requesterText[Notification::TYPE_EXPIRY_REQUESTER] = sprintf(
                        '<font color="#ff0000">Your hold-request is set to expire by %s. ' .
                        'Should you wish to extend it, please contact your administrator.</font>',
                        $expiryDate->format('l, d F y (T)'));
                }

                switch ($request->State) {
                    case 'Done':
                        if (!$notificationSent[Notification::TYPE_DONE_REQUESTER]->isSent()) {
                            $requesterText[Notification::TYPE_DONE_REQUESTER] =
                                'Your hold-request has now expired.';
                        }
                        break;

                    case 'Ready':
                        /** @noinspection PhpMissingBreakStatementInspection */
                    case 'ReadyForApproval':
                        if ($request->State === 'ReadyForApproval'
                            && (!$notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->isSent()
                                || $notificationSent[Notification::TYPE_PENDING_CUSTODIAN]->getDaysSince() > 7)) {
                            $custodianText[Notification::TYPE_PENDING_CUSTODIAN] =
                                'The above hold-request is waiting for review.';
                        }

                        if ((!$notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->isSent() && $daysSinceRequest > 7)
                            || $notificationSent[Notification::TYPE_PROGRESS_REQUESTER]->getDaysSince() > 7) {
                            $requesterText[Notification::TYPE_PROGRESS_REQUESTER] =
                                'Your hold-request is in progress.';
                        }

                    case 'Pending':
                        if (!$notificationSent[Notification::TYPE_CREATED_REQUESTER]->isSent()) {
                            $requesterText[Notification::TYPE_CREATED_REQUESTER] =
                                'Your hold-request has been received.';
                        }

                        if (!$notificationSent[Notification::TYPE_CREATED_CUSTODIAN]->isSent()) {
                            $custodianText[Notification::TYPE_CREATED_CUSTODIAN] =
                                'The above hold-request was received.';
                        }
                        break;

                    default:
                        break;
                }

                $requestDesc = sprintf(
                    '%s' .
                    'ID: %d<br>' .
                    'Time: %s<br>' .
                    'Expires On: %s<br>',
                    $request->ExtRef ? "Reference: {$request->ExtRef}<br>" : '',
                    $request->ID,
                    $requestTime->format('l, d F y H:i:s T'),
                    $expiryDate->format('l, d F y (T)')
                );

                $subject = sprintf('Data Hold Request - %sID:%d',
                    $request->ExtRef ? "Ref:{$request->ExtRef} " : '', $request->ID);

                if ($requesterText) {
                    ksort($requesterText);
                    $text = sprintf(
                        '<p>%s</p>' .
                        '<ul>%s%s</ul>' .
                        '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                        $requestDesc,
                        '<li>' . implode('</li><li>', $requesterText) . '</li>',
                        ($request->RejectedCount > 0) ? '<li>Some items were rejected during review.</li>' : '',
                        $orgId,
                        $request->ID
                    );

                    $userDetails = call_user_func($this->_cachedUsers, $request->UserID);
                    if (true !== $this->_dgapi->SendEmail(
                            $orgId, [[$userDetails->Name, $userDetails->UserName]], $subject, $text,
                            'Data Hold Request', null, null, true)) {
                        // could not send the notification; reset the array which later on gets looked up for "sent"
                        // notification, and saved to db.
                        $requesterText = [];
                    }
                }

                $custodianRecipients = [];
                if ($custodianText) {
                    ksort($custodianText);
                    $text = sprintf(
                        '<p>%s</p>' .
                        '<ul>%s</ul>' .
                        '<p>Please <a href="#org-%d-req-%d">click here</a> to take a detailed look.</p>',
                        $requestDesc, '<li>' . implode('</li><li>', $custodianText) . '</li>', $orgId, $request->ID
                    );

                    /** @var AutoApprovedPolicies $orgPolicies */
                    $orgPolicies = call_user_func($this->_cachedPolicies, $orgId);

                    $dsrPolicies = array_map(function ($row) {
                        return (int)$row->PolicyID;
                    }, $retentionDb->query(
                        'SELECT DISTINCT r.`PolicyID` PolicyID ' .
                        "FROM `ArchivingRetention`.`LegalHoldItems_{$orgId}` i" .
                        " JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                        '  ON i.`RetentionID`=r.`ID` ' .
                        "WHERE i.`LegalHoldID`={$request->ID}"
                    )->fetch_all());

                    $custodianRecipients = $orgPolicies->getCustodians($dsrPolicies);

                    // convert custodians user-id list into recipient list
                    // i.e [ui, u2] to [[u1-name, u1-email], [u2-name, u2-email]]
                    $notificationRecipients = array_map(function ($custodianUserId) {
                        $userDetails = call_user_func($this->_cachedUsers, $custodianUserId);

                        return [$userDetails->Name, $userDetails->UserName];
                    }, $custodianRecipients);

                    if (true !== $this->_dgapi->SendEmail(
                            $orgId, $notificationRecipients, $subject, $text,
                            'Data Hold Request', null, null, true))
                    {
                        // reset the flags so that they wouldn't get saved to db which will cause a later retry
                        $custodianText = [];
                    }
                }

                $retentionDb->StartTransaction();

                if ($request->State === 'Done') {
                    $retentionDb->query(
                        "UPDATE `ArchivingRetention`.`LegalHold_{$orgId}` " .
                        "SET `State` = 'Finalised' " .
                        "WHERE `ID` = {$request->ID}"
                    );
                } else if (array_key_exists(Notification::TYPE_EXPIRY_REQUESTER, $requesterText)) {
                    $retentionDb->query(
                        "UPDATE `ArchivingRetention`.`LegalHold_{$orgId}` " .
                        'SET `ExpiryNotified` = 1 ' .
                        "WHERE `ID` = {$request->ID}"
                    );
                }

                if ($custodianText || $requesterText) {
                    $notificationTypesSent = array_unique(array_merge(array_keys($custodianText),
                        array_keys($requesterText)));
                    $sqlValues = implode('), (',
                        array_map(function ($type) use ($request, $retentionDb, $orgId, $custodianRecipients) {
                            $notificationRecipientsStr = $retentionDb->real_escape_string(json_encode(
                            // type > 200, then notification is for requester otherwise custodians
                                ($type > 200) ? [$request->UserID] : $custodianRecipients
                            ));

                            return DsrType::LEGAL_HOLD . ", {$request->ID}, {$type}, '{$notificationRecipientsStr}'";
                        }, $notificationTypesSent));

                    $retentionDb->query(
                        "INSERT INTO `ArchivingRetention`.`DsrNotification_{$orgId}` (DsrType, ID, Type, Data)" .
                        " VALUES ({$sqlValues}) ON DUPLICATE KEY UPDATE `Time`=CURRENT_TIMESTAMP()"
                    );
                }

                $retentionDb->CommitTransaction();
            }
            catch (\Exception $e)
            {
                $retentionDb->RollbackTransaction();
                LogWarning($e);
                continue;
            }
        }
    }

    /**
     * Maintain a legal-hold cache for efficient lookup when accessing objects from archive
     *
     * @param int $orgId
     */
    private function _updateLegalHoldCache($orgId)
    {
        $retentionDb = \MySQLiTransactional::Get('retention');

        $internalDb = \MySQLiTransactional::Get('internal');

        try
        {
            $internalDb->StartTransaction();

            $activeLegalHolds = $retentionDb->query(
                'SELECT `ID`, `RestrictAccess` ' .
                "FROM `ArchivingRetention`.`LegalHold_{$orgId}` " .
                'WHERE `ExpiryDate` > CURDATE()' .
                " AND `State` = 'Ready'"
            )->fetch_all();

            $activeLegalHoldIds = [0];
            $activeRestrictedAccessLegalHoldIds = [0];
            foreach ($activeLegalHolds as $activeLegalHold) {
                $activeLegalHoldIds[] = (int)$activeLegalHold->ID;
                if ($activeLegalHold->RestrictAccess === 'yes') {
                    $activeRestrictedAccessLegalHoldIds[] = (int)$activeLegalHold->ID;
                }
            }

            $activeLegalHoldIdsStr = implode(',', $activeLegalHoldIds);
            $activeRestrictedAccessLegalHoldIdsStr = implode(',', $activeRestrictedAccessLegalHoldIds);

            $retentionDb->query(
                'SELECT' .
                ' li.`RetentionID` RetentionID, ' .
                " SUM(li.`State` = 'Approved' AND li.`LegalHoldID` IN ({$activeLegalHoldIdsStr})) LegalHoldCount, " .
                " SUM(li.`State` = 'Approved' AND li.`LegalHoldID` IN ({$activeRestrictedAccessLegalHoldIdsStr})) RestrictAccessCount " .
                "FROM `LegalHoldItems_{$orgId}` li " .
                'GROUP BY li.`RetentionID`',
                MYSQLI_USE_RESULT
            )->for_each_batch(function ($rows) use ($orgId, $internalDb) {
                $sql = 'INSERT INTO `ArchivingInternal`.`LegalHoldCache` ' .
                    '(`RetentionID`, `OrgID`, `LegalHoldCount`, `RestrictAccessCount`)' .
                    ' VALUES ' .
                    implode(', ', array_map(function ($row) use ($orgId) {
                            return "({$row->RetentionID}, {$orgId}, {$row->LegalHoldCount})";
                        }, $rows)
                    ) .
                    ' ON DUPLICATE KEY UPDATE ' .
                    ' `LegalHoldCount`=VALUES(`LegalHoldCount`), ' .
                    ' `RestrictAccessCount`=VALUES(`RestrictAccessCount`)';
                $internalDb->query($sql);
            }, 1000);
            $internalDb->CommitTransaction();
        }
        catch (\Exception $e)
        {
            $internalDb->RollbackTransaction();
            LogWarning($e);
        }
    }

    /**
     * Expire LegalHolds that are past their expiry-dates
     *
     * @param int $orgId
     */
    private function _expireLegalHold($orgId)
    {
        try {
            \MySQLiTransactional::Get('retention')->query(
                "UPDATE `ArchivingRetention`.`LegalHold_{$orgId}` " .
                'SET `State` = "Done" ' .
                'WHERE `ExpiryDate` <= CURDATE()' .
                ' AND `State` = "Ready"'
            );
        } catch (\Exception $e) {
            LogWarning($e);
        }
    }

    /**
     * Run what you like!
     * The return value is used as the exit code
     *
     * @return int
     */
    public function run()
    {
        $now = time();

        if ($this->_nextRunTime > $now)
        {
            return 0;
        }

        try
        {
            $this->_cachedPolicies->prune();

            $orgIds = $this->_getOrgIds();

            foreach ($orgIds as $orgId)
            {
                if (\DaemonScripts::DispatchAndSleep() && true === \DaemonScripts::Terminate())
                {
                    // Exit if Terminate was flagged
                    return 0;
                }

                $this->_expireLegalHold($orgId);

                $this->_processDeleteRequests($orgId);
                $this->_processLegalHoldRequests($orgId);

                if ($this->_timeOfNextNotificationRun <= $now) {
                    $this->_sendDeleteRequestNotifications($orgId);
                    $this->_sendLegalHoldNotifications($orgId);
                }

                if ($this->_timeOfNextLastLegalHoldCacheRun <= $now) {
                    $this->_updateLegalHoldCache($orgId);
                }
            }
        }
        catch (\Exception $e)
        {
            LogWarning($e);
        }

        $this->_updateTiming();

        return 0;
    }

    /**
     * Update timing variables
     */
    private function _updateTiming()
    {
        $now = time();
        $config = $this->getConf();

        if ($this->_nextRunTime <= $now)
        {
            $this->_nextRunTime = $now + $config->get('app.run_interval_seconds', 60);
        }

        if ($this->_timeOfNextLastLegalHoldCacheRun <= $now)
        {
            $this->_timeOfNextLastLegalHoldCacheRun =
                $now + $config->get('legal_hold.cache_update_interval_seconds', 600);
        }

        if ($this->_timeOfNextNotificationRun <= $now)
        {
            $this->_timeOfNextNotificationRun =
                $now + $config->get('dsr_notifications.check_interval_seconds', 300);
        }
    }
}
