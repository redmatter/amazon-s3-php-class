<?php

/**
 * PcapRecordingImportApp class
 *
 * PHP version 5
 *
 * @category  PcapRecordingImportApp
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

require_once "s3/S3.php";

/**
 * Class PcapRecordingImportApp
 *
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class PcapRecordingImportApp extends BaseDaemonApp
{
    const RUN_INTERVAL_SECONDS = 60;
    const CONFIG_PATH = '/etc/archiving/pcap-recording-import.conf';
    const CMD_ARCHIVE_CLI = '/usr/bin/archiving-cli';
    const CMD_PCAP_TO_WAV = '/usr/bin/pcaptowav';
    const ARCHIVE_MAGIC_HEADER = 'RMA-CE';

    private $_nextRunTime = 0;
    private $_apiAuth;
    private $_apiAuthSentry;

    /**
     * Authenticate to Core API using the details given
     *
     * @param string $coreapi   coreapi url
     * @param string $authCreds creds
     *
     * @return void
     *
     * @throws Exception
     */
    private function _coreApiLogin($coreapi, $authCreds)
    {
        API::Init($coreapi, new ArrayStorage());
        API::LoginBasicAuth($authCreds);

        $this->_apiAuthSentry = new ScopeSentry(
            function ()
            {
                API::LogOut();
            }
        );

        $this->_apiAuth = array($coreapi, API::GetToken());
    }

    /**
     * Logout from CoreAPI by resetting the sentry
     *
     * @return void
     */
    private function _coreApiLogout()
    {
        unset($this->_apiAuthSentry);
    }

    /**
     * Run what you like!
     * The return value is used as the exit code
     *
     * @return int
     * @throws Exception
     */
    public function run()
    {
        $now = time();
        if ($this->_nextRunTime > $now)
        {
            return 0;
        }

        $this->_nextRunTime = $now + self::RUN_INTERVAL_SECONDS;

        $authCreds = $this->getConf()->get('api.authorization_creds', false);
        if (!$authCreds)
        {
            throw new \Exception('No authorization creds or hash specified');
        }

        $this->_coreApiLogin($this->getConf()->get('api.base_url', 'http://api.redmatter.com'), $authCreds);

        $orgOneEncKey = ArchivingEndpoint::Get(
            $this->_apiAuth,
            1,
            ArchivingPolicy::Get($this->_apiAuth, 1, 0, 0)->storageEndpointID
        )->encryptionKey;


        $bigSql = MySQLiTransactional::Get('big', true);

        // read CSVs
        foreach ($this->_getCsvFiles($this->_getCsvDir()) as $csvFile)
        {
            try
            {
                $csvFileProgress = "$csvFile.progress";
                $offset = 0;
                if (file_exists($csvFileProgress))
                {
                    $offset = file_get_contents($csvFileProgress);
                    if (false === $offset)
                    {
                        throw new Exception("Failed to read $csvFileProgress");
                    }
                }

                $csvFileMetadata = "$csvFile.metadata";
                $csvMetadata = null;
                if (file_exists($csvFileMetadata))
                {
                    $csvMetadata = json_decode(file_get_contents($csvFileMetadata));
                }
                if (null === $csvMetadata)
                {
                    throw new Exception("Cannot read $csvFileMetadata");
                }

                if (!isset($csvMetadata->OrgID))
                {
                    throw new Exception('Could not find OrgID from CSV metadata');
                }
                if (!isset($csvMetadata->ArchivePolicyIDField))
                {
                    $csvMetadata->ArchivePolicyIDField = 'Archive Policy ID';
                }
                if (!isset($csvMetadata->PcapUuidField))
                {
                    $csvMetadata->PcapUuidField = 'UUID';
                }
                if (!isset($csvMetadata->LegUuidField))
                {
                    $csvMetadata->LegUuidField = $csvMetadata->PcapUuidField;
                }
                if (!isset($csvMetadata->LegField))
                {
                    $csvMetadata->LegField = 'Leg';
                }

                $sentry = new ScopeSentry(
                    function () use (&$fp)
                    {
                        if ($fp)
                        {
                            fclose($fp);
                            unset($fp);
                        }
                    }
                );

                $fp = fopen($csvFile, 'r');
                if (false === $fp)
                {
                    throw new Exception("Unable to open $csvFile");
                }

                $keys = fgetcsv($fp);
                if (!$keys)
                {
                    throw new Exception("Unable to read headers from $csvFile");
                }
                if (!in_array($csvMetadata->ArchivePolicyIDField, $keys))
                {
                    throw new Exception("ArchivePolicyIDField: '{$csvMetadata->ArchivePolicyIDField}' not found in CSV");
                }
                if (!in_array($csvMetadata->PcapUuidField, $keys))
                {
                    throw new Exception("UuidField: '{$csvMetadata->PcapUuidField}' not found in CSV");
                }
                if (!in_array($csvMetadata->LegUuidField, $keys))
                {
                    throw new Exception("LegUuidField: '{$csvMetadata->LegUuidField}' not found in CSV");
                }
                if (!in_array($csvMetadata->LegField, $keys))
                {
                    throw new Exception("LegField: '{$csvMetadata->LegField}' not found in CSV");
                }

                if ($offset)
                {
                    // seek to offset - sizeof(headers)
                    fseek($fp, $offset - ftell($fp));
                }

                $max_lines = 1000;
                while ($csv = fgetcsv($fp))
                {
                    $tempFiles = array();
                    $importSentry = new ScopeSentry(
                        function () use ($tempFiles) {
                            foreach ($tempFiles as $file)
                            {
                                @unlink($file);
                            }
                        }
                    );

                    try
                    {
                        $csv = array_combine($keys, $csv);

                        $leg = strtoupper($csv[$csvMetadata->LegField]);
                        if (!in_array($leg, array('A', 'B'), true))
                        {
                            throw new SkipCsvRecordException("Invalid value in LegFiled:'{$csvMetadata->LegField}'; found '$leg'");
                        }

                        // download PCAP
                        $pcapFile = $this->_getPcap($csv[$csvMetadata->PcapUuidField]);
                        $tempFiles[] = $pcapFile;

                        // because we are running the app with a system user CoreAPI credentials, ArchiveD will not let us
                        // download the decrypted version. It is assumed that the client has access to encryption key
                        // in order to decrypt it (which we do)
                        if ($this->_isArchive($pcapFile))
                        {
                            LogVerbose("PCAP {$csv[$csvMetadata->PcapUuidField]} downloaded as archive format; converting to PCAP");
                            $pcap = $this->_extractArchive($pcapFile, $orgOneEncKey);
                            @unlink($pcapFile);
                            $pcapFile = $pcap;
                        }

                        // extract WAV from PCAP
                        $recordingFile = $this->_extractWavFromPcap($pcapFile);
                        $tempFiles[] = $recordingFile;

                        $archivePolicy = ArchivingPolicy::Get($this->_apiAuth, $csvMetadata->OrgID, $csv[$csvMetadata->ArchivePolicyIDField], 0);
                        $storageEndpoint = ArchivingEndpoint::Get($this->_apiAuth, $csvMetadata->OrgID, $archivePolicy->storageEndpointID);

                        // convert WAV to archive-format
                        $archiveFile = $this->_makeArchive(
                            $recordingFile,
                            $storageEndpoint->compressed, $storageEndpoint->encrypted, Archiving::SUBFORMAT_STR_WAV,
                            $storageEndpoint->encryptionKey
                        );
                        $tempFiles[] = $archiveFile;

                        // find ownership info from Org_XX.Logs and calculate retention
                        // then form the metadata for further processing
                        $metadata = $this->_getCallMetadata(
                            Archiving::FILE_TYPE_RECORDING,
                            $csvMetadata->OrgID,
                            UUID::v4(),
                            Archiving::GetFileFormat(true, true, Archiving::SUBFORMAT_WAV),
                            $archivePolicy,
                            filesize($recordingFile),
                            filesize($archiveFile),
                            $leg,
                            $csv[$csvMetadata->LegUuidField]
                        );

                        // - make S3 headers from above metadata
                        $recordingStorageHeaders = $this->_getStorageHeadersFromCallMetadata($metadata);
                        // upload to S3
                        $this->_putToStorage(
                            $storageEndpoint,
                            Archiving::ConstructIdentifier(Archiving::FILE_TYPE_RECORDING, $csvMetadata->OrgID, $metadata['UUID']),
                            $archiveFile,
                            $recordingStorageHeaders
                        );

                        $bigSql->StartTransaction();

                        // - make retention record from above metadata
                        $recordingRetentionRecord = $this->_getRetentionRecordFromCallMetadata($metadata);
                        // add retention record
                        $sql = "INSERT INTO `ArchivingRetention`.`Retention_{$csvMetadata->OrgID}` (" .
                            '`' . implode('`, `', array_keys($recordingRetentionRecord)) . '`' .
                            ') VALUES (' . implode(', ', $recordingRetentionRecord) . ')';
                        LogDebug($sql);
                        $bigSql->query($sql);

                        // add usage record (batched)
                        $sql = "INSERT INTO ArchivingRetention.Usage_{$csvMetadata->OrgID} (" .
                            ' `Hour`, `Type`, `OrgID`, `Count`, `ClearUsage`, `RealUsage`, `EndpointID` ' .
                            ') VALUES (' .
                            'DATE_ADD(CURDATE(), INTERVAL HOUR(NOW()) HOUR), "' . implode(
                                '", "',
                                array(
                                    Archiving::GetFileTypeStr($metadata['FileType']),
                                    $metadata['OrgId'],
                                    1,
                                    $metadata['ClearSize'],
                                    $metadata['RealSize'],
                                    $metadata['Policy']->storageEndpointID
                                )
                            ) . '") ON DUPLICATE KEY UPDATE' .
                            ' `Count`=`Count`+VALUES(`Count`),' .
                            ' `ClearUsage`=`ClearUsage`+VALUES(`ClearUsage`),' .
                            ' `RealUsage`=`RealUsage`+VALUES(`RealUsage`)';
                        LogDebug($sql);
                        $bigSql->query($sql);

                        // Set the leg in Call-Logs to be recorded and update the Archive Policy ID
                        $archPolIdFromOrTo = ($metadata['Leg'] === 'A') ? 'From' : 'To';
                        if ($metadata['CallLogPolArchID'][$archPolIdFromOrTo] && $metadata['CallLogPolArchID'][$archPolIdFromOrTo] != $csv[$csvMetadata->ArchivePolicyIDField])
                        {
                            LogWarning("CallLog:{$archPolIdFromOrTo}_PolArchID is set and not equal to FromCSV:{$csv[$csvMetadata->ArchivePolicyIDField]}");
                        }
                        $sql = "UPDATE `Org_{$csvMetadata->OrgID}`.`Logs`" .
                            " SET `Recorded`='YES'," .
                            " `{$archPolIdFromOrTo}_PolArchID`={$csv[$csvMetadata->ArchivePolicyIDField]} " .
                            " WHERE `{$metadata['Leg']}UUID`=0x" . str_replace('-', '', $metadata['LegUUID']);
                        LogDebug($sql);
                        $bigSql->query($sql);

                        $bigSql->CommitTransaction();
                    }
                    catch (SkipCsvRecordException $e)
                    {
                        LogMessage($e);
                        LogMessage("Skipping...");
                    }

                    unset($importSentry);
                    if (!$max_lines--)
                    {
                        break;
                    }
                }

                $file_done = $csv === false && feof($fp);
                if (!$file_done)
                {
                    if (false === file_put_contents($csvFileProgress, ftell($fp)))
                    {
                        throw new Exception("Failed to save to $csvFileProgress");
                    }
                }
                // if we did read from .progress, i.e the file was present, and now that we are done; unlink it
                elseif ($offset)
                {
                    unlink($csvFileProgress);
                }

                // close the file
                unset($sentry);

                if ($file_done)
                {
                    rename($csvFile, dirname($csvFile) . '/done/' . basename($csvFile));
                    rename($csvFileMetadata, dirname($csvFileMetadata) . '/done/' . basename($csvFileMetadata));
                }
            }
            catch (Exception $e)
            {
                LogError($e);
            }
        }

        $this->_coreApiLogout();

        return 0;
    }

    /**
     * Show usage help, optionally with error message
     *
     * @param string $error error
     *
     * @return void
     */
    public function showUsage($error = '')
    {
        $self = basename(__FILE__, '.php');
        if ($error)
        {
            $error = "\n\nERROR: $error\n\n";
        }

        echo <<<HELP

Archiving PCAP Recording Import
(C) Red Matter Ltd. UK$error

Usage: $self [--conf=/path/to/run.conf] [--no-daemon]

HELP;
    }

    /**
     * Get CSV files from the directory, sorted by in-progress and mtime
     *
     * @param string $directory the directory
     *
     * @return array
     */
    private function _getCsvFiles($directory)
    {
        $csvFiles = array_map(
            function ($file) {
                return array($file, filemtime($file), file_exists("{$file}.progress"));
            },
            array_filter(
                glob("$directory/*.csv", GLOB_MARK),
                function ($file) {
                    return '/' !== substr($file, -1);
                }
            )
        );

        usort(
            $csvFiles,
            function ($a, $b)
            {
                // if both equal WRT progress file
                if ($a[2] === $b[2])
                {
                    // compare mtime of the file
                    return $a[1] - $b[1];
                }
                // else either A or B has a progress file

                // if A has progress file, let that be the first
                if ($a[2])
                {
                    return -1;
                }

                // otherwise B
                return 1;
            }
        );

        return array_map(
            function ($item) {
                return $item[0];
            },
            $csvFiles
        );
    }

    /**
     * Get CSV dir from config; verified the dir to be in working order
     *
     * @return string
     * @throws Exception
     */
    private function _getCsvDir()
    {
        $csvDir = $this->getConf()->get('pcap_import.csv_dir');
        if (null === $csvDir)
        {
            throw new \RuntimeException("app.csv_import_dir not set in config");
        }
        if (!is_dir($csvDir) || !is_writable($csvDir) || !is_readable($csvDir))
        {
            throw new \Exception("app.csv_import_dir:$csvDir doesnt exist or has incorrect permissions");
        }

        if (!@is_dir("$csvDir/done/"))
        {
            if (!@mkdir("$csvDir/done/") && !@is_dir("$csvDir/done/"))
            {
                throw new \Exception("Could not create dir: \"$csvDir/done\"");
            }
        }
        if (!is_writable("$csvDir/done/") || !is_readable("$csvDir/done/"))
        {
            throw new \Exception("app.csv_import_dir:$csvDir/done/ has incorrect permissions");
        }

        return $csvDir;
    }

    /**
     * Download PCAP from Archive
     *
     * @param string $uuid PCAP UUID
     *
     * @return string PCAP file path
     *
     * @throws APIAuthRequiredException
     * @throws Exception
     */
    private function _getPcap($uuid)
    {
        if (!is_executable(self::CMD_ARCHIVE_CLI))
        {
            throw new Exception(self::CMD_ARCHIVE_CLI . ' not found');
        }

        $pcapFile = new TempPath("pcap_{$uuid}.", $this->getConf()->get('app.temp_path', '/tmp'));
        $command = $this->_formatCommand(
            self::CMD_ARCHIVE_CLI,
            array(
                'download',
                array('--identifier', "pcap/1/$uuid"),
                array('--output', $pcapFile->getPath()),
                array('--archived-host', $this->_getArchiveDHost()),
                array('--access-reason', 'PCAP to Recording Import'),
                array('--api-host', $this->_getApiHost()),
                array('--api-token', API::GetToken()),
                array('--temp-dir', $this->getConf()->get('app.temp_dir')),
                array('--log.syslog-tag', APP_NAME_),
                array('--log.level', $this->getConf()->get('log.level', 4)),
            )
        );
        if (0 !== $this->_executeCommand($command))
        {
            unset($pcapFile);
            throw new SkipCsvRecordException("Unable to download PCAP:{$uuid}");
        }

        $pcapFile->setRetain();

        return $pcapFile->getPath();
    }

    /**
     * Extract WAV from PCAP
     *
     * @param string $pcapFile PCAP file path
     *
     * @return string WAV file path
     * @throws Exception
     */
    private function _extractWavFromPcap($pcapFile)
    {
        $wavFile = "$pcapFile.wav";
        $sentry = new ScopeSentry(
            function () use ($wavFile)
            {
                @unlink($wavFile);
            }
        );
        if (0 !== $this->_executeCommand($this->_formatCommand(self::CMD_PCAP_TO_WAV, array($pcapFile, $wavFile))))
        {
            unset($sentry);
            throw new SkipCsvRecordException("Unable to extract WAV from PCAP:{$pcapFile}");
        }

        $sentry->cancel();

        return $wavFile;
    }

    /**
     * Make archive file
     *
     * @param string $filePath     path to file to archive
     * @param bool   $isCompressed to be compressed? (WIP)
     * @param bool   $isEncrypted  to be encrypted? (WIP)
     * @param int    $subFormat    content-type
     * @param string $encKey       base64 of encryption key
     *
     * @return string archive (RMA-CE) file path
     * @throws Exception
     */
    private function _makeArchive($filePath, $isCompressed, $isEncrypted, $subFormat, $encKey)
    {
        // todo convert-to-archive should support optionally compressing and encrypting
        unset($isCompressed, $isEncrypted);

        $archiveFile = new TempPath(basename("{$filePath}.archive."), $this->getConf()->get('app.temp_path', '/tmp'));
        $command = $this->_formatCommand(
            self::CMD_ARCHIVE_CLI,
            array(
                'convert-to-archive',
                array('--input', $filePath),
                array('--output', $archiveFile->getPath()),
                array('--content-type', $subFormat),
                array('--encryption-key', $encKey),
                array('--temp-dir', $this->getConf()->get('app.temp_dir')),
                array('--log.syslog-tag', APP_NAME_),
                array('--log.level', $this->getConf()->get('log.level', 4)),
            )
        );
        if (0 !== $this->_executeCommand($command))
        {
            unset($archiveFile);
            throw new SkipCsvRecordException("Unable to make archive file:{$filePath}");
        }

        $archiveFile->setRetain();

        return $archiveFile->getPath();
    }

    /**
     * Extract contents of archive
     *
     * @param string $filePath file path
     * @param string $encKey   encryption key
     *
     * @return string
     * @throws Exception
     */
    private function _extractArchive($filePath, $encKey)
    {
        $clearFile = new TempPath(basename("{$filePath}.clear."), $this->getConf()->get('app.temp_path', '/tmp'));
        $command = $this->_formatCommand(
            self::CMD_ARCHIVE_CLI,
            array(
                'convert-to-clear',
                array('--input', $filePath),
                array('--output', $clearFile->getPath()),
                array('--encryption-key', $encKey),
                array('--temp-dir', $this->getConf()->get('app.temp_dir')),
                array('--log.syslog-tag', APP_NAME_),
                array('--log.level', $this->getConf()->get('log.level', 4)),
            )
        );
        if (0 !== $this->_executeCommand($command))
        {
            unset($clearFile);
            throw new SkipCsvRecordException("Unable to extract archive file:{$filePath}");
        }

        $clearFile->setRetain();

        return $clearFile->getPath();
    }

    /**
     * Piece together metadata from Org_XX.Logs table and other sources passed in
     *
     * @param int             $fileType      file-type
     * @param int             $orgId         org ID
     * @param string          $uuid          recording uuid
     * @param int             $fileFormat    file-format after archiving (ArchiveD lingo)
     * @param ArchivingPolicy $archivePolicy archive policy
     * @param int             $clearSize     clear/plain file size
     * @param int             $realSize      archive file size
     * @param string          $leg           A or B
     * @param string          $legUuid       call leg UUID
     *
     * @return array
     * @throws Exception|SkipCsvRecordException
     * @throws MySQLiErrorException
     */
    private function _getCallMetadata(
        $fileType,
        $orgId,
        $uuid,
        $fileFormat,
        ArchivingPolicy $archivePolicy,
        $clearSize,
        $realSize,
        $leg,
        $legUuid
    )
    {
        $bigsql = MySQLiConnection::Get('big');
        $res = $bigsql->query(
            'SELECT `Time`, `Recorded`, `From_PolArchID`, `To_PolArchID`, `From_UserID`, `To_UserID` ' .
            " FROM `Org_{$orgId}`.`Logs` " .
            " WHERE `{$leg}UUID`=0x" . str_replace('-', '', $legUuid)
        )->fetch_all(MySQLiResult::AS_ASSOC);
        switch (count($res)) {
            case 1:
                break;
            case 0:
                throw new SkipCsvRecordException("No call entries found for BUUID=$legUuid");
            default:
                throw new SkipCsvRecordException("More than one call entries found for BUUID=$legUuid");
        }
        $record = reset($res);

        $metadata = array(
            'FileType' => $fileType,
            'OrgId' => $orgId,
            'UUID' => $uuid,
            'Leg' => $leg,
            'LegUUID' => $legUuid,
            // todo This mapping based on Leg, might not be appropriate (need to review)
            'UserId' => ($leg === 'A') ? $record['From_UserID'] : $record['To_UserID'],
            'FileFormat' => $fileFormat,
            'CreationTime' => $record['Time'],
            'RetentionMaxTime' => $this->_getTimeOffset($record['Time'], $archivePolicy->retentionMax),
            'RetentionMinTime' => $this->_getTimeOffset($record['Time'], $archivePolicy->retentionMin),
            'Policy' => $archivePolicy,
            'ClearSize' => $clearSize,
            'RealSize' => $realSize,
            'CallLogRecordedFlag' => $record['Recorded'] === 'YES',
            'CallLogPolArchID' => array('From'=>$record['From_PolArchID'], 'To'=>$record['To_PolArchID']),
        );
        LogDebug(array('Metadata'=>$metadata));
        return $metadata;
    }

    /**
     * Get time offset by (N, unit)
     *
     * @param int   $time   epoch time
     * @param array $offset offset specified as [N, unit]
     *
     * @return int offset epoch time
     * @throws Exception
     */
    private function _getTimeOffset($time, array $offset)
    {
        $datetime = new DateTime('@' . $time, new DateTimeZone('UTC'));
        $offsetDatetime = $datetime->add(
            DateInterval::createFromDateString(
                sprintf('%d %s', $offset[0], $offset[1])
            )
        );
        if (false === $offsetDatetime)
        {
            throw new SkipCsvRecordException("Failed to calculate time-offset");
        }

        return $offsetDatetime->getTimestamp();
    }

    /**
     * Get UTC date from epoch time
     *
     * @param int $time epoch time
     *
     * @return string date as YYYY-MM-DD
     */
    private function _getUtcDate($time)
    {
        $datetime = new DateTime('@' . $time, new DateTimeZone('UTC'));
        return $datetime->format('Y-m-d');
    }

    /**
     * Form S3 headers from metadata
     *
     * @param array $metadata metadata
     *
     * @return array
     */
    private function _getStorageHeadersFromCallMetadata(array $metadata)
    {
        /*
        // Example
        x-amz-meta-file-type: wav
        x-amz-meta-file-type-is-compressed: true
        x-amz-meta-file-type-is-encrypted: true
        x-amz-meta-leg-uuid: b0000000-0939-d2a4-20b8-558d33a08554
        x-amz-meta-creation-date: 2015-06-26
        x-amz-meta-retention-max-date: 2015-12-27
        x-amz-meta-retention-min-date: 2015-12-27
        x-amz-meta-policy-id: 5
        x-amz-meta-policy-version: 2
        x-amz-meta-real-size: 19285712
        x-amz-meta-clear-size: 19200044

        x-amz-meta-qa-created-by: pcap-recording-import
        */
        $headers = array(
            'file-type' => Archiving::GetSubFormatStr(Archiving::GetSubFormatFromFormat($metadata['FileFormat'])),
            'file-type-is-compressed' => Archiving::IsCompressedFormat($metadata['FileFormat']) ? 'true' : 'false',
            'file-type-is-encrypted' => Archiving::IsEncryptedFormat($metadata['FileFormat']) ? 'true' : 'false',
            'leg-uuid' => $metadata['LegUUID'],
            'creation-date' => $this->_getUtcDate($metadata['CreationTime']),
            'retention-max-date' => $this->_getUtcDate($metadata['RetentionMaxTime']),
            'retention-min-date' => $this->_getUtcDate($metadata['RetentionMinTime']),
            'policy-id' => $metadata['Policy']->id,
            'policy-version' => $metadata['Policy']->version,
            'clear-size' => $metadata['ClearSize'],
            'real-size' => $metadata['RealSize'],
            // in case in the future if we have to differentiate these files in storage or if we ever notice any anomaly
            'created-by' => 'pcap-recording-import',
        );
        LogDebug(array('S3Headers'=>$headers));
        return $headers;
    }

    /**
     * Form retention record from metadata
     *
     * @param array $metadata metadata
     *
     * @return array
     */
    private function _getRetentionRecordFromCallMetadata(array $metadata)
    {
        $retentionRecord = array(
            'UUID' => "UNHEX(REPLACE(\"{$metadata['UUID']}\",\"-\",\"\"))",
            'LegUUID' => "UNHEX(REPLACE(\"{$metadata['LegUUID']}\",\"-\",\"\"))",
            'Format' => $metadata['FileFormat'],
            'Type' => $metadata['FileType'],
            'OrgID' => $metadata['OrgId'],
            'UserID' => $metadata['UserId'],
            'PolicyID' => $metadata['Policy']->id,
            'PolicyVersion' => $metadata['Policy']->version,
            'CreationDate' => '"'.$this->_getUtcDate($metadata['CreationTime']).'"',
            'RetentionMaxDate' => '"'.$this->_getUtcDate($metadata['RetentionMaxTime']).'"',
            'RetentionMinDate' => '"'.$this->_getUtcDate($metadata['RetentionMinTime']).'"',
            'MidnightOffsetSeconds' => $metadata['CreationTime'] % 86400 /*(24 * 60 * 60)*/,
            'EndpointID' => $metadata['Policy']->storageEndpointID,
            'State' => '"Available"',
            'Purged' => '"no"',
        );
        LogDebug(array('RetentionRecord'=>$retentionRecord));
        return $retentionRecord;
    }

    /**
     * Save archive file to storage
     *
     * @param ArchivingEndpoint $endpoint   storage endpoint
     * @param string            $identifier object key
     * @param string            $file       file on disk
     * @param array             $headers    storage headers
     *
     * @return bool
     * @throws Exception
     */
    private function _putToStorage(ArchivingEndpoint $endpoint, $identifier, $file, array $headers)
    {
        switch ($endpoint->type)
        {
            case 's3':
                $s3 = new S3($endpoint->accessKey, $endpoint->secretKey);
                break;

            case 's3-emulation':
                if (1 !== preg_match('/^([^\/]+)(.*)$/', $endpoint->baseURI, $matches))
                {
                    throw new Exception("Malformed BaseURI '{$endpoint->baseURI}' in storage endpoint");
                }
                $s3 = new S3($endpoint->accessKey, $endpoint->secretKey, false, S3::BUCKET_URI_STYLE_PATH, $matches[1], $matches[2]);
                break;

            default:
                throw new SkipCsvRecordException("Storage Endpoint Type '{$endpoint->type}' not supported");
        }
        return $s3->putObjectFile($file, $endpoint->bucket, $identifier, S3::ACL_PRIVATE, $headers);
    }

    /**
     * Format command, piecing together a base command and its arguments
     *
     * @param string $baseCommand base command
     * @param array  $args        args
     *
     * @return string
     */
    private function _formatCommand($baseCommand, array $args)
    {
        return $baseCommand . ' ' . implode(
            ' ',
            array_map(
                function ($arg)
                {
                    return is_array($arg) ?
                        // of form --option=value
                        ("{$arg[0]}=\"" . addslashes($arg[1]) . "\"") :
                        // of form --option or a sub-command
                        ('"' . addslashes($arg) . '"');
                },
                $args
            )
        );
    }

    /**
     * Execute the command specified and return the exit code
     *
     * @param string $command command
     *
     * @return int
     */
    private function _executeCommand($command)
    {
        $output = array();
        $exitCode = 1;
        LogDebug($command);
        exec($command, $output, $exitCode);
        // ignore the output
        if ($output)
        {
            LogDebug($output);
        }
        unset($output);

        return $exitCode;
    }

    /**
     * Get ArchiveD Hostname
     *
     * @return string
     * @throws Exception
     */
    private function _getArchiveDHost()
    {
        return $this->_getHostFromUrl($this->getConf()->get('archived.base_url'));
    }

    /**
     * Get Core API Hostname
     *
     * @return string
     * @throws Exception
     */
    private function _getApiHost()
    {
        return $this->_getHostFromUrl($this->getConf()->get('api.base_url'));
    }

    /**
     * Get hostname from URL
     *
     * @param string $url url
     *
     * @return string
     * @throws Exception
     */
    private function _getHostFromUrl($url)
    {
        if (1 !== preg_match('/([a-z]+:\\/\\/)*([^\\/]+)/', $url, $m))
        {
            throw new Exception('Failed to match the URL');
        }
        return $m[2];
    }

    /**
     * Check the magic header and see if the file is in archive format
     *
     * @param string $file file path
     *
     * @return bool
     * @throws Exception
     */
    private function _isArchive($file)
    {
        $fp = fopen($file, 'r');
        if ($fp === false)
        {
            throw new SkipCsvRecordException("Unable to open $file");
        }

        $magic = fread($fp, strlen(self::ARCHIVE_MAGIC_HEADER));
        fclose($fp);

        return ($magic === self::ARCHIVE_MAGIC_HEADER);
    }
}

/**
 * Class SkipCsvRecordException
 *
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class SkipCsvRecordException extends Exception
{
}