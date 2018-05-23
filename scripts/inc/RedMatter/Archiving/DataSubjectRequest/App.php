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

/**
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class App extends \BaseDaemonApp
{
    const CONFIG_PATH = '/etc/archiving/dsr-preproc.conf';

    private $_timeOfNextLastLegalHoldCacheRun = 0;

    private $_cachedPolicies;

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
        $this->_cachedPolicies->prune();

        $config = $this->getConf();

        $coreapi = $config->get('api.base_url', 'http://api');
        \API::Init($coreapi, new \ArrayStorage(), APP_NAME_.':1.0');

        if ($authCreds = $config->get('api.authorization_creds', false))
        {
            \API::LoginBasicAuth($authCreds, null/*mfa*/, true/*re_authenticate*/);
        }
        else
        {
            /** @noinspection ExceptionsAnnotatingAndHandlingInspection */
            throw new \RuntimeException('No authorization creds specified in config');
        }

        $api = array($coreapi, \API::GetToken());
        /** @noinspection PhpUnusedLocalVariableInspection */
        /** @noinspection OnlyWritesOnParameterInspection */
        $authorizationSentry = new \ScopeSentry(
            function() use (&$api)
            {
                unset($api);
                \API::LogOut();
            }
        );

        $orgIds = $this->_getOrgIds();

        foreach ($orgIds as $orgId)
        {
            $this->_processDeleteRequests($orgId);
            $this->_processLegalHoldRequests($orgId);

            $now = time();

            // ------------- Cache status of legal-hold for individual items ------------

            if ($this->_timeOfNextLastLegalHoldCacheRun < $now)
            {
                $internalDb = \MySQLiTransactional::Get('internal');
                $retentionDb = \MySQLiTransactional::Get('retention');

                $internalDb->StartTransaction();
                $retentionDb->query('SELECT li.`RetentionID` RetentionID, ' .
                    'SUM(li.`LegalHoldID` IN (' .
                    " SELECT `ID` FROM `ArchivingRetention`.`LegalHold_{$orgId}` WHERE `ExpiryDate` > CURDATE()" .
                    ')) LegalHoldCount ' .
                    "FROM `LegalHoldItems_{$orgId}` li " .
                    'GROUP BY li.`RetentionID`'
                )->for_each_batch(function ($rows) use ($orgId, $internalDb) {
                    $sql = 'INSERT INTO `ArchivingInternal`.`LegalHoldCache` (`RetentionID`, `OrgID`, `LegalHoldCount`)' .
                        ' VALUES ' .
                        implode(', ', array_map(function ($row) use ($orgId) {
                                return "({$row->RetentionID}, {$orgId}, {$row->LegalHoldCount})";
                            }, $rows)
                        ) .
                    ' ON DUPLICATE KEY UPDATE `LegalHoldCount`=VALUES(`LegalHoldCount`)';
                    $internalDb->query($sql);
                }, 1000);
                $internalDb->CommitTransaction();

                $this->_timeOfNextLastLegalHoldCacheRun = $now + $config->get('legal_hold.cache_update_interval_minutes', 10) * 60;
            }
        }

        return 0;
    }
}
