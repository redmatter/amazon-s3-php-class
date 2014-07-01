<?php
/**
 * dev-setup-v2.php
 *
 * PHP version 5
 *
 * @category  Meh
 * @package   Meh
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

define('APP_NAME_', 'rm-archdevsetup');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_RECURSIVE_LOOP_SAFETY', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);

require_once 'rmapp/class.BaseApp.php';

/**
 * MakePurgeDataApp
 *
 * @category  Meh
 * @package   Meh
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class ArchivingDevSetupApp extends BaseApp
{
	/**
	 * Run what you like!
	 * The return value is used as the exit code
	 *
	 * @throws Exception
	 * @return integer
	 */
	public function run()
	{
		$config = $this->conf;

		$org = cli::get('--org', false);
		if (!$org)
			throw new Exception('No Org ID specified');

		$authUser = cli::get('--api-auth-username', $config->get('api.authorization_username', false));
		if (!$authUser)
			throw new Exception('No username specified');

		$authPass = cli::get('--api-auth-password', $config->get('api.authorization_password', 'letmein'));

		API::Init($config->get('api.url', 'api'), new ArrayStorage());
		API::Login($authUser, $authPass);
		$sentry = new ScopeSentry(
			function()
			{
				API::LogOut();
			}
		);

		try
		{
			API::Get("/archiving/provisioning/{$org}");
		}
		catch (Exception $e)
		{
			LogConsole("Org $org not provisioned; doing it now!");
			API::Put('/archiving/provisioning', '<?xml version="1.0" encoding="UTF-8"?><Archiving><Provisioning><OrgID>'.$org.'</OrgID></Provisioning></Archiving>');
		}

		$xpath = API::Get("/archiving/policy/{$org}/0");
		if (is_null($defaultPolicyId = $xpath('/Archiving/Policy/ID')))
			LogConsole('Unable to find PolicyID', 1);

		$xpath = API::Get('/archiving/defaultpolicy/'.$org);

		// default policy is already looked up; so we are good with that
		$policyOk = array($defaultPolicyId => true);

		/*
		 * Check policy and set it to null if bad
		 *
		 * @param int $policyId
		 *
		 * @return bool
		 */
		$checkPolicy = function(&$policyId) use ($org, &$policyOk)
		{
			try
			{
				if (isset($policyOk[$policyId]))
					return $policyOk[$policyId];

				API::Get("/archiving/policy/$org/$policyId");
				$policyOk[$policyId] = true;
			}
			catch (Exception $e)
			{
				MessageLog("Invalid policy $policyId for Org $org");
				$policyId = null;
				$policyOk[$policyId] = false;
			}

			return $policyOk[$policyId];
		};

		$recordingDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="recording"]/@PolicyID');
		if (!is_null($recordingDefPolicy))
			$checkPolicy($recordingDefPolicy);

		$smsDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="sms"]/@PolicyID');
		if (!is_null($smsDefPolicy))
			$checkPolicy($smsDefPolicy);

		$cdrDefPolicy = $xpath('/Archiving/DefaultPolicies/DefaultPolicy[@Type="cdr"]/@PolicyID');
		if (!is_null($cdrDefPolicy))
			$checkPolicy($cdrDefPolicy);

		if (is_null($recordingDefPolicy) || is_null($smsDefPolicy) || ($org == 1 && is_null($cdrDefPolicy)))
		{
			$body = new DOMMaker('Archiving');
			$defPols = $body->nodeCreate('DefaultPolicies');
			$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'recording', 'PolicyID' => is_null($recordingDefPolicy)? $defaultPolicyId: $recordingDefPolicy));
			$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'sms', 'PolicyID' => is_null($smsDefPolicy)? $defaultPolicyId: $smsDefPolicy));
			if ($org == 1)
				$body->nodeCreate('DefaultPolicy', null, $defPols, array('Type'=>'cdr', 'PolicyID' => is_null($cdrDefPolicy)? $defaultPolicyId: $cdrDefPolicy));

			API::Post('/archiving/defaultpolicy/'.$org, $body->getDOMDocument()->saveXML());
		}

		// set a predictable but random Encryption Key for the Org
		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000001 for Org 1
		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000002 for Org 2
		// 69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A0000000003 for Org 3
		// ... etc
		MySQLiConnection::Get('core')
			->query(
				sprintf(
					"UPDATE `Archiving`.`Provisioning` SET EncryptionKey=UNHEX(\"69FF46F93F05BEAA210A04BFE2350C01A93A6CF1D24E9B5D14929A%010d\")".
					" WHERE `OrgID` = %d LIMIT 1",
					$org,
					$org
				)
			);

		$res = MySQLiConnection::Get('retention')->query(
			"SELECT `TABLE_NAME` FROM `information_schema`.`TABLES` t".
			" WHERE t.`TABLE_SCHEMA`='ArchivingRetention' AND t.`TABLE_NAME` LIKE '%%_Template'"
		)->fetch_all();

		foreach ($res as $row)
		{
			$tableName = str_replace('_Template', "_$org", $row->TABLE_NAME);
			MySQLiConnection::Get('retention')->query("CREATE TABLE IF NOT EXISTS `ArchivingRetention`.`$tableName` LIKE `ArchivingRetention`.`{$row->TABLE_NAME}`");
		}
	}
}

try
{
	$default_conf = getcwd().'/dev-setup.conf';
	if (false === CLI::get('--conf', false) && !is_readable($default_conf))
		throw new Exception("No configuration given and no config found at $default_conf; use --conf option to specify the config file");

	$app = ArchivingDevSetupApp::getInstance(CLI::get('--conf', $default_conf));

	if (true === CLI::get('--help'))
	{
		$prog = basename(__FILE__, '.php');
		echo <<<USAGE
Setup Archiving for a given Org

$prog --api-auth-username=< Username > [--api-auth-password=< Password >] --org=< OrgID >

    --org=< OrgID >
        OrgID we are working on

    --api-auth-username=< Username >
        API Username

    --api-auth-password=< Password >
        API Password; assumes "letmein", if not given

    --conf=< ConfigPath >
        Path to a config file

    --help
	    This help message

USAGE;
		exit(0);
	}

	$app->run();
	exit(0);
}
catch (Exception $e)
{
	LogWarning($e);
	exit(1);
}
