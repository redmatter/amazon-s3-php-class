#!/usr/bin/php54
<?php

/**
 * ArchivingSmsRecovery
 *
 * PHP version 5
 *
 * @category Library
 * @package  RM-archiving
 * @author   Andrew Bulford <andrew.bulford@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @version  SVN: $Id: $
 * @link     http://trac.redmatter.com/
 */
define('APP_NAME_', 'rm-archiving-sms-recovery');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);
//define('RM_CLI_APP', true);

require_once 'rmapp/class.BaseApp.php';
require_once 's3/S3.php';

/**
 * Instructions that should be followed prior to running this script:
 * - Add the following line to /etc/RedMatterDBCreds:
 * 		archiving:Aux:[password]:bigsql-rw.[searchdomain]
 * Replacing [password] and [searchdomain] with sensible values.
 *
 * - Add the following GRANTs:
 *   Big DB:
 * 		GRANT SELECT ON `Aux`.* TO 'archiving'@'[hostname]'
 * 		GRANT SELECT, UPDATE ON `Org\_%`.* TO 'archiving'@'[hostname]'
 *   Core DB:
 *		GRANT SELECT ON `Archiving`.`Provisioning` TO 'cdruser'@'hostname';
 * Replacing [hostname] with the hostname of the box where the script will be run
 *
 * Revert these changes once the recovery has been completed.
 */


/**
 * ArchivingSmsRecovery
 *
 * @category Archiving
 * @package  RM-archiving
 * @author   Andrew Bulford <andrew.bulford@redmatter.com>
 * @license  http://www.redmatter.com/license private
 * @link     http://www.redmatter.com
 */
class ArchivingSmsRecoveryApp extends BaseApp
{
    protected $org = null;
    protected $api = null;
    protected $endpoint_details = array();
    protected $endpoints = array();

	/**
	 * run
	 *
	 * @throws Exception
	 * @throws MySQLiErrorException
	 *
	 * @return void
	 */
    public function run()
    {
        $config = $this->conf;

		if (false === ($auth_creds = cli::get('--auth-creds', $config->get('api.authorization_creds', false))))
		{
			if (false === ($auth_hash = cli::get('--auth-hash', $config->get('api.authorization_hash', false))))
				throw new Exception('No authorization creds or hash specified');
		}

        if (false === ($this->org = cli::get('--org', $config->get('extract.org_id', false))))
            throw new Exception('OrgID not specified');

        if (false === ($batch_size = cli::get('--batch-size', $config->get('extract.batch_size', false))))
            throw new Exception('Batch size not specified');

		$max_batches = cli::get('--max-batches', $config->get('extract.max_batches', 0));
		$no_limit = $max_batches == 0;

		LogConsole(
			"Starting SMS recovery with ".
			"Org ID: {$this->org}, ".
			"batch size: {$batch_size}, ".
			"max batches: " . ($no_limit ? "unlimited" : $max_batches)
		);

		// Log in to API
        $authorization = null;
        $coreapi = $config->get('api.base_url', 'http://cdr.coreapi.redmatter.com');
        
		if ($auth_creds)
		{
			if (strpos($auth_creds, ' ') === false) 
				// no scheme specified, assume it's basic auth
				$auth_creds = "Basic {$auth_creds}";
			$auth_hdrs = array('Authorization' => $auth_creds);
			$auth_url = "{$coreapi}/auth";
		} 
		else 
		{
			// use old auth hash scheme
			$auth_hdrs = null;
			$auth_url = "{$coreapi}/auth/{$auth_hash}";
		}

		$stream = HTTPStream::Make('GET', $auth_url, null/*body*/, $auth_hdrs);
        if ($stream && $stream->httpCode == 201)
		{
            $xpath = new XPath($stream->getContents());
            $authorization = $xpath('/Success/ResourceID');
			LogMessage('Authenticated');
        }
        
        if (!$authorization)
		{
            throw new Exception('Failed to authenticate using the credentials given');
        }

        $this->api = array($coreapi, $authorization);

        // Create a ScopeSentry - which will log us out when we exit this scope.
        /** @noinspection PhpUnusedLocalVariableInspection */
        $authorization_sentry = new ScopeSentry(
            function () use ($authorization, $coreapi) {
                HTTPStream::Make('DELETE', "{$coreapi}/auth", null, array('Authorization' => $authorization));
				LogMessage('Auth deleted', $authorization);
            }
        );

		// Get Core SQL connection
		$core_dbc = MySQLiConnection::Get('coresql');

		// Get the encryption key for the Org
		$result = $core_dbc->query(
			"SELECT `EncryptionKey` FROM `Archiving`.`Provisioning` WHERE `OrgID` = {$this->org}"
		)->fetch_all(MySQLiResult::AS_ASSOC);

		if (empty($result) || empty($result[0]['EncryptionKey']))
			throw new Exception('Could not get encryption key for Org');

		$encryption_key = $result[0]['EncryptionKey'];

        // Get Big SQL database connection
        $big_dbc = MySQLiConnection::Get('bigsql');

		$batch_number = 0;
		$total_processed = 0;
		do
		{
			LogConsole("Batch {$batch_number}");

			// Get UUIDs of SMSLogs which are missing both To_Number and From_Number
			$results = $big_dbc->query(
				"SELECT HEX(`sms`.`UUID`) as `UUID`, `arc`.`PolicyID`, `arc`.`PolicyVersion`\n" .
				"FROM `Org_{$this->org}`.`SMSLogs` `sms`\n" .
				"JOIN `ArchivingRetention`.`Retention_{$this->org}` `arc` ON `sms`.`UUID` = `arc`.`UUID`\n" .
				"WHERE `sms`.`To_Number` = '' AND `sms`.`From_Number` = ''".
				"LIMIT {$batch_size}"
			)->fetch_all(MySQLiResult::AS_ASSOC);

			$num_results = count($results);
			LogMessage("{$num_results} SMSLogs returned");

			// For each SMS log missing both To and From numbers...
			foreach ($results as $result)
			{
				// Convert UUID to original format
				$hex_uuid = $result['UUID'];
				$uuid = strtolower($hex_uuid);
				$uuid = substr($uuid, 0, 8) .
					'-' . substr($uuid, 8, 4) .
					'-' . substr($uuid, 12, 4) .
					'-' . substr($uuid, 16, 4) .
					'-' . substr($uuid, 20);

				// Get endpoint details from API
                list($bucket, $s3creds) = $this->getS3Endpoint($result['PolicyID'], $result['PolicyVersion']);

				// Construct storage key from UUID
				$storage_key = Archiving::FILE_TYPE_STR_SMS . "/{$this->org}/{$uuid}";

				// GET record from S3
				LogMessage("Downloading {$storage_key} from S3 Bucket {$bucket}");

				$response = S3::getObject($bucket, $storage_key, false, null, $s3creds);
				if (!isset($response->code) || $response->code !== 200)
				{
					$message = "There was a problem downloading {$storage_key} from S3";
					if (isset($response->error))
						$message .= ": {$response->error}";

					LogMessage($message);
					continue;
				}

				if (!isset($response->headers) || !is_array($response->headers))
				{
					LogMessage("{$storage_key} was successfully downloaded from S3 but had no headers; skipping");
					continue;
				}

				if (empty($response->body))
				{
					LogMessage("{$storage_key} GET was successful but no body was returned");
					continue;
				}

				LogMessage("Successfully downloaded {$storage_key} from S3 Bucket {$bucket}");

				// Decrypt record
				try
				{
					LogMessage("Processing {$storage_key}");
					$extracted_content = ArchivingProcessFile::ProcessFileContent($response->body, $encryption_key);
					LogMessage("Successfully processed {$storage_key}");
				}
				catch (Exception $e)
				{
					LogError("Failed to process content of {$storage_key}: {$e}");
					continue;
				}

				// Parse XML
				$xml = new DOMDocument();
				if ($xml->loadXML($extracted_content) === false)
				{
					LogError("Failed to parse processed XML from {$storage_key}");
					continue;
				}
				$xpath = new DOMXPath($xml);

				// Get To_Number and From_Number
				$to_number = $xpath->query('/SMS/ToNumber');
				$from_number = $xpath->query('/SMS/FromNumber');

				if (!($to_number instanceof DOMNodeList) ||
					!($from_number instanceof DOMNodeList))
				{
					LogError("Failed to query ToNumber or FromNumber in XML for {$storage_key}");
					continue;
				}

				if ($to_number->length != 1 || $from_number->length != 1)
				{
					LogError(
						"Unexpected number of ToNumber or FromNumber elements in XML for {$storage_key}"
					);
					continue;
				}

				$to_number = $to_number->item(0)->textContent;
				$from_number = $from_number->item(0)->textContent;

				LogMessage("ToNumber: {$to_number}, FromNumber: {$from_number}");

				// Update SMSLog record
				try
				{
					$query = "UPDATE `Org_{$this->org}`.`SMSLogs`\n" .
						"SET `To_Number` = '{$to_number}', `From_Number` = '{$from_number}'\n".
						"WHERE `UUID` = UNHEX('{$hex_uuid}')\n".
						"LIMIT 1";
					$big_dbc->query($query);
				}
				catch (Exception $e)
				{
					LogError("Exception caught while updating SMSLog with UUID {$uuid}");
					continue;
				}
				++$total_processed;
			}
			$batch_number++;
		}
		while ($num_results == $batch_size && ($no_limit || ($batch_number < $max_batches)));

		LogConsole("Finished - Total processed: {$total_processed}");
    }

    /**
     * getEndpointDetails
     * Singleton accessor for endpoints so they are only retrieved once per policy and policy version
     *
     * @param int $policy_id      Policy ID
     * @param int $policy_version Policy Version
     *
     * @throws Exception
     *
     * @return ArchivingEndpoint
     */
    protected function getEndpointDetails($policy_id, $policy_version)
    {
        if (!isset($this->endpoint_details[$policy_id][$policy_version]))
		{
            $policy = ArchivingPolicy::Get($this->api, $this->org, $policy_id, $policy_version);
            $endpoint_details = ArchivingEndpoint::Get($this->api, $this->org, $policy->storageEndpointID, true);

            if (!isset($this->endpoint_details[$policy_id]))
			{
                $this->endpoint_details[$policy_id] = array();
            }

            $this->endpoint_details[$policy_id][$policy_version] = $endpoint_details;
        }

        return $this->endpoint_details[$policy_id][$policy_version];
    }

    /**
     * getS3Endpoint
     *
     * @param int $policy_id      Policy ID
     * @param int $policy_version Policy Version
     *
     * @throws Exception
     *
     * @return [S3BucketConfig, S3Credentials]
     */
    protected function getS3Endpoint($policy_id, $policy_version)
    {
        if (!isset($this->endpoints[$policy_id][$policy_version]))
		{
            $endpoint_details = $this->getEndpointDetails($policy_id, $policy_version);

            if ($endpoint_details->type !== 's3')
			{
                throw new Exception('Endpoint is not on Amazon S3');
            }

            if (!isset($this->endpoints[$policy_id]))
			{
                $this->endpoints[$policy_id] = array();
            }

			$this->endpoints[$policy_id][$policy_version] = array(
				new S3BucketConfig($endpoint_details->bucket, $endpoint_details->region),
				new S3Credentials($endpoint_details->accessKey, $endpoint_details->secretKey)
			);
        }

        return $this->endpoints[$policy_id][$policy_version];
    }
}

/**
 * usage
 *
 * @return void
 */
function usage()
{
	$script_name = basename(__FILE__);
	echo <<<USAGE

Archiving SMS Recovery Tool
Fixes SMSLogs records with missing To_Number and From_Number values by retrieving them from S3

{$script_name} [options]

Available options:

    --org=< OrgID >
        ID of the Org for which SMSLogs should be repaired

    --batch-size=< N >
        Batch size of the set of SMSLogs to be recovered

    --max-batches=< N >
        Maximum number of batches to process (0 means no maximum)

    --auth-creds=< creds >
        API auth creds base64(username:password)

    --auth-hash=< Hash >
        API auth hash SHA1(username).SHA1(password)

    --conf=< ConfigPath >
        Path to a config file; sample file available at
        /usr/share/archiving/archiving-sms-recovery.conf.sample

    --help
        This help message


USAGE;
	exit(0);
}

try
{
	if (CLI::get('--help', false) !== false)
		usage();

    $default_conf = getenv('PWD').'/archiving-sms-recovery.conf';
	$config = CLI::get('--conf', $default_conf);
	if (!is_readable($config))
        throw new Exception("No configuration given and no config found at {$default_conf}; use --conf option to specify the config file");

	ArchivingSmsRecoveryApp::getInstance($config)->run();
}
catch (Exception $e)
{
    LogWarning($e);
	usage();
}
