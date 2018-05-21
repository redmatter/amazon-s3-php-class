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

/**
 * Class MakePurgeVerifyCsvApp
 *
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
        $config = $this->getConf();

        $assocTypes = \Archiving::GetFileTypesAssociationMap();

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

        $retentionDb = \MySQLiTransactional::Get('retention');

        $orgIds = [];
        // query information-schema to figure out OrgIDs to work with, partitioned across data-centres
        $retentionDb->query(
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

        foreach ($orgIds as $orgId)
        {
            // list of auto-approval enabled policy-ids
            // [p1, p2]
            $autoApprovePolicies = [];
            // an array of array with user-id as key and array policy-ids he/she is a custodian of
            // [ u1 => [p1, p2], ... ]
            $policyCustodians = [];
            /** @var \XPath $xpath */
            $xpath = \API::Get("/archiving/policy/{$orgId}/all");
            /** @var \DOMNodeList $policyNodes */
            $policyNodes = $xpath('/Archiving/Policies/Policy', true);
            foreach ($policyNodes as $policyNode)
            {
                $policyId = (int)$xpath('ID', false, $policyNode);

                if ('yes' === $xpath('AutoApproveDataSubjectRequest', false, $policyNode))
                {
                    $autoApprovePolicies[] = $policyId;
                }

                $custodians = $xpath('DataCustodians/DataCustodian/@UserID', true, $policyNode);
                if ($custodians !== null)
                {
                    $policyCustodians[$policyId] = [];
                    /** @var \DOMNodeList $custodians */
                    foreach ($custodians as $userId)
                    {
                        $userId = (int)$userId;

                        if (!isset($policyCustodians[$userId]))
                        {
                            $policyCustodians[$userId] = [];
                        }

                        $policyCustodians[$userId][] = $policyId;
                    }
                }
            }

            $dsdrs = $retentionDb->query(
                'SELECT `ID`, `UserID` ' .
                "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                "WHERE `State` = 'Pending' LIMIT 10"
            )->fetch_all();

            foreach ($dsdrs as $dsdr) {

                $dsdrId = (int)$dsdr->ID;
                $dsdrUserId = (int)$dsdr->UserID;

                // ------------- add-associated filetypes ------------

                foreach ($assocTypes as $type => $assoc) {
                    $assocStr = implode(', ', $assoc);
                    $retentionDb->query(
                        "INSERT INTO `ArchivingRetention`.`DeleteRequestItems_{$orgId}` " .
                        ' (`DeleteRequestID`, `OrgID`, `RetentionID`) ' .
                        "SELECT {$dsdrId}, {$orgId}, r_.`ID`" .
                        ' FROM ' .
                        "`ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                        "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                        ' ON i.`RetentionID` = r.`ID` ' .
                        "JOIN `ArchivingRetention`.`Retention_{$orgId}` r_ " .
                        ' ON r.`UUID` = r_.`LegUUID` ' .
                        "WHERE i.`DeleteRequestID` = {$dsdrId} " .
                        " AND r.`Type` = {$type} " .
                        " AND r_.`Type` IN ($assocStr)"
                    );
                }

                // ------------- auto-approve items ------------

                if (!isset($policyCustodians[$dsdrUserId]) && !count($autoApprovePolicies))
                {
                    // we have nothing to auto-approve
                    continue;
                }

                if (isset($policyCustodians[$dsdrUserId]))
                {
                    // if the user who made the request is a data-custodian, and the request contains items archived
                    // under one of the policies that he/she is a data-custodian of, then those items should be
                    // auto-approved
                    $autoApprovePoliciesStr = implode(', ', array_unique(array_merge($autoApprovePolicies, $policyCustodians[$dsdrUserId])));
                }
                else
                {
                    // otherwise auto-approve only the ones under policies with auto-approval enabled
                    $autoApprovePoliciesStr = implode(', ', $autoApprovePolicies);
                }

                $retentionDb->query(
                    "UPDATE `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    "JOIN `ArchivingRetention`.`Retention_{$orgId}` r " .
                    ' ON i.`RetentionID` = r.`ID` ' .
                    "SET i.`State` = 'Approved' " .
                    "WHERE i.`DeleteRequestID` = {$dsdrId} " .
                    " AND i.`State` = 'Pending' " .
                    " AND r.`PolicyID` IN ({$autoApprovePoliciesStr})"
                );

                $counts = $retentionDb->query(
                    'SELECT COUNT(*) Count ' .
                    "FROM `ArchivingRetention`.`DeleteRequestItems_{$orgId}` i " .
                    "WHERE i.`State` = 'Pending'" .
                    " AND i.`DeleteRequestID` = {$dsdrId}"
                )->fetch_all();

                $newState = $counts[0]->Count? 'ReadyForApproval': 'ReadyForPurge';
                $retentionDb->query(
                    "UPDATE `ArchivingRetention`.`DeleteRequest_{$orgId}` dr " .
                    "SET dr.`State` = '{$newState}' " .
                    "WHERE dr.`ID` = {$dsdrId}"
                );
            }

            $now = time();
            if ($this->_timeOfNextLastLegalHoldCacheRun < $now)
            {
                $internalDb = \MySQLiTransactional::Get('internal');

                $internalDb->StartTransaction();
                $retentionDb->query('SELECT li.`RetentionID` RetentionID, ' .
                    'SUM(li.`LegalHoldID` IN (' .
                    " SELECT `ID` FROM `ArchivingRetention`.`LegalHold_{$orgId}` WHERE `ExpiryDate` > CURDATE()" .
                    ')) LegalHoldCount' .
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
