<?php
/**
 * App.php
 *
 * PHP version 5
 *
 * @category  App.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\PurgeVerificationCsv;

use JsonSchema\RefResolver as JsonSchemaRefResolver;
use JsonSchema\Uri\UriResolver as JsonSchemaUriResolver;
use JsonSchema\Uri\UriRetriever as JsonSchemaUriRetriever;
use JsonSchema\Validator as JsonSchemaValidator;
use RedMatter\Archiving\PurgeVerificationCsv\Definition\Dataset;
use RedMatter\Archiving\PurgeVerificationCsv\Helpers\Adaptors\DateRange;
use RedMatter\Archiving\PurgeVerificationCsv\Helpers\DateRangeIterator;

/**
 * Class MakePurgeVerifyCsvApp
 *
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class App extends \BaseApp
{
    const CONFIG_PATH = '/etc/archiving/make-purge-verification-csv.conf';
    const SCHEMA_FILE = 'definition-schema.json';
    const MYSQL_DATE_FORMAT = 'Y-m-d';
    const MIN_CREATION_DATE = '2010-06-25';
    const MAX_CREATION_DATE = 'now';
    const GROUP_CONCAT_BUF_LENGTH = 1024;

    private $allOrgIds;

    /**
     * Run what you like!
     * The return value is used as the exit code
     *
     * @throws \RuntimeException
     * @throws \JsonMapper_Exception
     * @throws \Exception
     * @throws \MySQLiErrorException
     * @throws \CLIUsageErrorException
     *
     * @return int
     */
    public function run()
    {
        if (false === ($definitionFile = \CLI::Get('definition', false))) {
            throw new \CLIUsageErrorException('definition not given');
        }
        if (false === ($definition = file_get_contents($definitionFile))) {
            throw new \RuntimeException("Unable to read definition file $definitionFile");
        }

        $ok = false;
        if (null !== ($definition = json_decode($definition))) {
            $ok = $this->validateDefinition($definition);
        }
        if (false !== \CLI::Get('validate', false)) {
            echo $ok ? 'Definition is correctly formatted' : 'Invalid definition';
            exit($ok ? 0 : 1);
        } elseif (!$ok) {
            throw new \RuntimeException("Invalid JSON in definition file $definitionFile");
        }

        $definition = $this->loadDefinition($definition);

        $idx = 1;
        $outputDir = \CLI::Get('output-dir', getcwd());
        if (!is_writable($outputDir)) {
            LogConsole("Output directory not writable ($outputDir)");
            LogConsole('Use --output-dir option to specify a different location');
            return 1;
        }
        foreach ($definition->Datasets as $dataset) {
            LogConsole('Dataset: '.json_encode($dataset));
            $file = "{$outputDir}/".strtolower($dataset->ExpectedOutcome).sprintf('_%03d.csv', $idx++);
            LogConsole("Writing CSV to $file");
            $this->makeCsvFromDataset($dataset, $file);
        }

        return 0;
    }

    /**
     * Validate the purge definition against schema
     *
     * @param \stdClass $definition purge definition
     *
     * @return bool
     * @throws \RuntimeException
     */
    private function validateDefinition($definition)
    {
        $refResolver = new JsonSchemaRefResolver(new JsonSchemaUriRetriever(), new JsonSchemaUriResolver());
        $schema = $refResolver->resolve('file://' . $this->getSchemaFile());

        $validator = new JsonSchemaValidator();
        $validator->check($definition, $schema);

        if ($validator->isValid()) {
            return true;
        }

        file_put_contents('php://stderr', json_encode($validator->getErrors(), JSON_PRETTY_PRINT));

        return false;
    }

    /**
     * Get the schema file; either the DEV version or installed version
     * Prefers DEV version
     *
     * @return string
     * @throws \RuntimeException
     */
    private function getSchemaFile()
    {
        global $argv;

        $script = realpath($argv[0]);
        $schema = dirname($script).'/'.self::SCHEMA_FILE;
        if (file_exists($schema)) {
            return $schema;
        }
        $schema = '/usr/share/archiving/purge-verify-'.self::SCHEMA_FILE;
        if (file_exists($schema)) {
            return $schema;
        }

        throw new \RuntimeException('Unable to find schema file');
    }

    /**
     * Load the object we got from JSON-Decode using Object Mapper
     *
     * @param \stdClass $definition definition
     *
     * @return Definition
     * @throws \JsonMapper_Exception
     */
    private function loadDefinition($definition)
    {
        $mapper = new \JsonMapper();
        return $mapper->map($definition, new Definition());
    }

    /**
     * Make CSV from a given Dataset definition
     *
     * @param Dataset $dataset dataset
     * @param string  $file    file to which the CSV is to be written
     *
     * @throws \Exception
     * @throws \MySQLiErrorException
     * @throws \RuntimeException
     *
     * @return void
     */
    private function makeCsvFromDataset(Dataset $dataset, $file)
    {
        $whereOtherCriteria = $this->getWhereFromSelectionCriteria(
            $this->getAllCriteria($dataset, ['OrgID', 'CreationDate'])
        );

        $dateCriterion = $this->getCreationDateCriterion($dataset);
        if ($dateCriterion && !$dateCriterion->Range) {
            throw new \RuntimeException('Values list not allowed for CreationDate criteria');
        }

        // get absolute Min and Max (overridable from CLI)
        $absoluteMinCreationDate = \CLI::Get('min-creation-date', self::MIN_CREATION_DATE);
        $absoluteMaxCreationDate = \CLI::Get('max-creation-date', self::MAX_CREATION_DATE);

        // if either Min or Max is not given, substutute that with absoluteMin or absoluteMax
        $creationDateMin = DateRange::getUtcDateTimeFromString($dateCriterion->Range->Min ?: $absoluteMinCreationDate);
        $creationDateMax = DateRange::getUtcDateTimeFromString($dateCriterion->Range->Max ?: $absoluteMaxCreationDate);

        foreach ($this->getSelectedOrgs($dataset, $this->getAllOrgIds()) as $orgId) {
            $rangeInterval = null;
            // if the Org has more than 99999 retention records, it will be safer
            // to iterate over the given date range in month chunks
            if ($this->getOrgRetentionRecordCount($orgId) > 99999) {
                $rangeInterval = \DateInterval::createFromDateString('1 month');
            }

            // create the date range iterator from given criteria
            $dateRangeIterator = new DateRangeIterator(
                new DateRange($creationDateMin, $creationDateMax),
                $dateCriterion->Exclude,
                DateRange::fromUtcDateStrings($absoluteMinCreationDate, $absoluteMaxCreationDate),
                $rangeInterval
            );

            $db = $this->getRetentionDb();
            foreach ($dateRangeIterator as $dateRange) {
                if ($dataset->SelectionMode === 'ALL') {
                    $this->generateCsvFromSql($orgId, $whereOtherCriteria, $file);
                } else {
                    $db->query('SET @@group_concat_max_len='.self::GROUP_CONCAT_BUF_LENGTH);
                    $sql = 'SELECT GROUP_CONCAT(`ID` ORDER BY RAND()) IDs ' .
                        "FROM `ArchivingRetention`.`Retention_{$orgId}` " .
                        'WHERE ' .
                        " `CreationDate` >= '{$dateRange->Min->format('Y-m-d')}' " .
                        " AND `CreationDate` < '{$dateRange->Max->format('Y-m-d')}' " .
                        ($whereOtherCriteria ? " AND $whereOtherCriteria " : '').
                        'GROUP BY CreationDate, PolicyID, EndpointID, Type';
                    LogConsole($sql);
                    $db->query($sql)->for_each(
                        function (\MySQLiResult_Row $row) use ($orgId, $file) {
                            // remove the last ID if the length of the IDs list (string) is very close to
                            // GROUP_CONCAT_BUF_LENGTH
                            $ids = $row->IDs;
                            if (strlen($ids) > (self::GROUP_CONCAT_BUF_LENGTH - 20)) {
                                $ids = preg_replace('/,\\d*$/', '', $ids);
                            }
                            $this->generateCsvFromSql($orgId, "`ID` IN ( {$ids} )", $file);
                        }
                    );
                }
            }
        }
    }

    /**
     * Get all OrgIDs from database
     *
     * @return array empty array if no OrgId was found
     *
     * @throws \MySQLiErrorException
     */
    private function getAllOrgIds()
    {
        if ($this->allOrgIds) {
            return $this->allOrgIds;
        }

        $res = $this->getRetentionDb()->query(
            'SELECT SUBSTRING(TABLE_NAME FROM 7) `OrgID` '.
            ' FROM `information_schema`.TABLES '.
            ' WHERE TABLES.TABLE_SCHEMA="ArchivingRetention"' .
            '  AND TABLE_NAME LIKE "Usage_%"' .
            '  AND TABLE_NAME != "Usage_Template"'
        )->fetch_all();
        $this->allOrgIds = array_map(
            function (\MySQLiResult_Row $row) {
                return (int)$row->OrgID;
            },
            $res
        );

        return $this->allOrgIds;
    }

    /**
     * Get retention database connection
     *
     * @return \MySQLiConnection|\MySQLiTransactional
     */
    private function getRetentionDb()
    {
        return \MySQLiConnection::Get('retention');
    }

    /**
     * Apply selection criteria and get a list of OrgIds
     *
     * @param Dataset $dataset definition
     * @param int[]   $orgIds  entire list of Orgs
     *
     * @return mixed
     */
    private function getSelectedOrgs(Dataset $dataset, array $orgIds)
    {
        $orgSelectionCriterion = array_filter(
            $dataset->Criteria,
            function (Dataset\Selection\Criterion $criterion) {
                return $criterion->Field === 'OrgID';
            }
        );
        $orgSelection = array_map(
            function (Dataset\Selection\Criterion $criterion) use ($orgIds) {
                if ($criterion->Exclude) {
                    return array_diff($orgIds, $criterion->Values);
                } else {
                    return $criterion->Values;
                }
            },
            $orgSelectionCriterion
        );

        return call_user_func_array('array_merge', $orgSelection);
    }

    /**
     * Get the CreationDate criterion if any
     *
     * @param Dataset $dataset dataset
     *
     * @return Dataset\Selection\Criterion|null
     * @throws \RuntimeException
     */
    private function getCreationDateCriterion(Dataset $dataset)
    {
        $criterion = array_filter(
            $dataset->Criteria,
            function (Dataset\Selection\Criterion $criterion) {
                return $criterion->Field === 'CreationDate';
            }
        );
        if (count($criterion) > 1) {
            throw new \RuntimeException('More than one "CreationDate" criteria found');
        } elseif (!$criterion) {
            return null;
        }

        return reset($criterion);
    }

    /**
     * Get Org RetentionRecord Count from peeking into MySQL index cardinality
     *
     * @param int $orgId org-id
     *
     * @return int
     * @throws \MySQLiErrorException
     */
    private function getOrgRetentionRecordCount($orgId)
    {
        $db = $this->getRetentionDb();
        $primaryIdx = $db
            ->query("SHOW INDEX FROM `ArchivingRetention`.`Retention_{$orgId}` WHERE Key_name='PRIMARY'")
            ->fetch_all();
        return (int)$primaryIdx['Cardinality'];
    }

    /**
     * Get criteria excluding the ones specified
     *
     * @param Dataset  $dataset       data set
     * @param string[] $ignoredFields ignored field names
     *
     * @return Dataset\Selection\Criterion[]
     */
    private function getAllCriteria(Dataset $dataset, $ignoredFields)
    {
        return array_filter(
            $dataset->Criteria,
            function (Dataset\Selection\Criterion $criterion) use ($ignoredFields) {
                /** @noinspection TypeUnsafeArraySearchInspection */
                return !in_array($criterion->Field, $ignoredFields);
            }
        );
    }

    /**
     * Get SQL Where clause for given criteria
     *
     * @param Dataset\Selection\Criterion[] $criteria criteria
     *
     * @return string
     * @throws \RuntimeException
     */
    private function getWhereFromSelectionCriteria(array $criteria)
    {
        return implode(
            ' AND ',
            array_map(
                function (Dataset\Selection\Criterion $criterion) {
                    if ($criterion->Range) {
                        throw new \RuntimeException("Range not allowed for Field: {$criterion->Field}");
                    }
                    $in = $criterion->Exclude ? 'NOT IN' : 'IN';
                    return "`{$criterion->Field}` {$in} ( \"".implode('","', $criterion->Values)."\" )";
                },
                $criteria
            )
        );
    }

    /**
     * Generates SQL as expected by the verify script, from the given SQL
     * If the file diesn't exist on disk already, it is created and headers are added.
     *
     * @param int    $orgId org id
     * @param string $where sql
     * @param string $file  file path
     *
     * @throws \RuntimeException
     * @throws \MySQLiErrorException
     *
     * @return void
     */
    private function generateCsvFromSql($orgId, $where, $file)
    {
        $newFile = !file_exists($file);
        if (false === ($fp = fopen($file, 'a'))) {
            throw new \RuntimeException("Unable to write file:$file");
        }

        /** @noinspection PhpUnusedLocalVariableInspection */
        /** @noinspection OnlyWritesOnParameterInspection */
        $sentry = new \ScopeSentry('fclose', [$fp]);

        if ($newFile) {
            $headers = [
                'Type/OrgID/UUID',
                'PolicyID',
                'PolicyVersion',
                'CreationDate',
                'RetentionMinDate',
                'RetentionMaxDate',
                'EndpointID',
                'PurgedByDate',
                'PurgedByPolicyVersion',
            ];
            if (false === fputcsv($fp, $headers)) {
                throw new \RuntimeException("Unable to write file:$file");
            }
        }
        $db = $this->getRetentionDb();
        $sql = 'SELECT' .
            ' LCASE(CONCAT(' .
            "  HEX(SUBSTR(`UUID`, 1, 4)),'-'," .
            "  HEX(SUBSTR(`UUID`, 5, 2)),'-'," .
            "  HEX(SUBSTR(`UUID`, 7, 2)),'-'," .
            "  HEX(SUBSTR(`UUID`, 9, 2)),'-'," .
            '  HEX(SUBSTR(`UUID`, 11))' .
            ')) `UUID`, ' .
            ' `Type`, `PolicyID`, `PolicyVersion`, `CreationDate`, ' .
            ' `RetentionMinDate`, `RetentionMaxDate`, `EndpointID` ' .
            "FROM `ArchivingRetention`.`Retention_{$orgId}` " .
            "WHERE $where ";
        LogConsole($sql);
        $db->query($sql)->for_each(
            function ($row) use ($orgId, $fp, $file) {
                $record = [
                    \Archiving::GetFileTypeStr($row['Type']).'/'.$orgId.'/'.$row['UUID'],
                    $row['PolicyID'],
                    $row['PolicyVersion'],
                    $row['CreationDate'],
                    $row['RetentionMinDate'],
                    $row['RetentionMaxDate'],
                    $row['EndpointID'],
                    $row['Type'],
                    '',
                    '',
                ];
                if (false === fputcsv($fp, $record)) {
                    throw new \RuntimeException("Unable to write file:$file");
                }
            },
            \MySQLiResult::AS_ASSOC
        );
    }
}
