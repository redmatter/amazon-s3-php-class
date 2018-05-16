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
            $autoApprovePolicies = [];
            /** @var \XPath $xpath */
            $xpath = \API::Get("/archiving/policy/{$orgId}/all");
            /** @var \DOMNodeList $policyNodes */
            $policyNodes = $xpath('/Archiving/Policies/Policy', true);
            foreach ($policyNodes as $policyNode)
            {
                if ('yes' === $xpath('AutoApproveDataSubjectRequest', false, $policyNode))
                {
                    $autoApprovePolicies[] = (int)$xpath('ID', false, $policyNode);
                }
            }

            if (!count($autoApprovePolicies))
            {
                continue;
            }

            $autoApprovePoliciesStr = implode(', ', $autoApprovePolicies);
            $assocTypes = \Archiving::GetFileTypesAssociationMap();

            $dsdrIds = [];
            $retentionDb->query(
                'SELECT `ID` ' .
                "FROM `ArchivingRetention`.`DeleteRequest_{$orgId}` " .
                "WHERE `State` = 'Pending' LIMIT 10"
            )->for_each(function ($row) {
                $dsdrIds[] = (int)$row->ID;
                return true;
            });

            foreach ($dsdrIds as $dsdrId) {

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
                if (!$counts[0]->Count)
                {
                    $retentionDb->query(
                        "UPDATE `ArchivingRetention`.`DeleteRequest_{$orgId}` dr " .
                        "SET dr.`State` = 'ReadyForPurge' " .
                        "WHERE dr.`ID` = {$dsdrId}"
                    );
                }
            }
        }

        return 0;
    }
}
