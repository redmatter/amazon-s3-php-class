<?php

/**
 * Controller using the REST architecture
 *
 * PHP version 5
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
 * Controller
 *
 * @category  Controllers
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class ArchivingPolicy
{
	public $id;
	public $org;
	public $version;
	public $name;
	public $retentionMin;
	public $retentionMax;
	public $reducedRedundancy;
	public $mode;
	public $storageEndpointID;
	public $ownerUser;
	public $notifyDataCustodians;
	public $askReasonForAccess;
	public $users;
	public $groups;
	public $dataCustodians;
	public $allowUnrestrictedAccess;
	public $disallowRecordingDownload;
	
	private static $_objs = array();

	/**
	 * Cached getter of instance
	 *
	 * @param dbc $dbc dbc
	 * @param int $org org
	 * @param int $id  id
	 *
	 * @return ArchivingPolicy
	 */
	public static function Get($dbc, $org, $id, $version)
	{
		if (!isset(self::$_objs[$org]))
			self::$_objs[$org] = array();
		
		if (!isset(self::$_objs[$org][$id]))
		{
			self::$_objs[$org][$id] = new ArchivingPolicy($dbc, $org, $id, $version);
			if($id <= 0)
				self::$_objs[$org][self::$_objs[$org][$id]->id] =& self::$_objs[$org][$id];
		}

		return self::$_objs[$org][$id];
	}

	/**
	 * updateFromDb
	 * 
	 * @param object $dbc database connection object
	 *
	 * @return bool
	 */
	function updateFromDb(MySQLiConnection $dbc)
	{
		if ($this->version)
			$sql = "SELECT ( OrgID=0 ) SystemPolicy, ((OrgID=0) OR (Version NOT IN (SELECT MAX(Version) FROM Archiving.PolicyVersions WHERE ID={$this->id}))) ReadOnly,  Version, OrgID, Name, RetentionMin, RetentionMinUnit, RetentionMax, RetentionMaxUnit, ReducedRedundancy, Mode, StorageEndpointID, OwnerUserCanRead, OwnerUserCanDelete, AskReasonForAccess, Description, NotifyDataCustodians, AllowUnrestrictedAccess, DisallowRecordingDownload FROM Archiving.PolicyVersions WHERE OrgID={$this->org} AND ID={$this->id} HAVING Version={$this->version}";
		else
			$sql = "SELECT ( OrgID=0 ) SystemPolicy, ( OrgID=0 ) ReadOnly, Version, OrgID, Name, RetentionMin, RetentionMinUnit, RetentionMax, RetentionMaxUnit, ReducedRedundancy, Mode, StorageEndpointID, OwnerUserCanRead, OwnerUserCanDelete, AskReasonForAccess, Description, NotifyDataCustodians, AllowUnrestrictedAccess, DisallowRecordingDownload FROM Archiving.Policy WHERE OrgID={$this->org} AND ID={$this->id}";
		LogDebug($sql, 'sql');
		$res = $dbc->query($sql)->fetch_all();
		if (sizeof($res) > 0)
		{
			$row = $res[0];
			$this->name = $row->Name;
			$this->version = $row->Version;
			$this->retentionMin = array($row->RetentionMin, $row->RetentionMinUnit);
			$this->retentionMax = array($row->RetentionMax, $row->RetentionMaxUnit);
			$this->reducedRedundancy = $row->ReducedRedundancy;
			$this->mode = $row->Mode;
			$this->storageEndpointID = $row->StorageEndpointID;
			$this->ownerUser = array('CanRead'=>$row->OwnerUserCanRead, 'CanDelete'=>$row->OwnerUserCanDelete);
			$this->askReasonForAccess = $row->AskReasonForAccess;
			$this->description = $row->Description;
			$this->notifyDataCustodians = $row->NotifyDataCustodians;
			$this->systemPolicy = ($row->SystemPolicy == 1);
			$this->allowUnrestrictedAccess = $row->AllowUnrestrictedAccess;
			$this->disallowRecordingDownload = $row->DisallowRecordingDownload;

			$this->users = $dbc->query("SELECT UserID, CanRead, CanDelete FROM Archiving.PolicyUsers WHERE PolicyID={$this->id}")->fetch_all();
			$this->groups = $dbc->query("SELECT GroupID, CanRead, CanDelete FROM Archiving.PolicyGroups WHERE PolicyID={$this->id}")->fetch_all();
			$this->dataCustodians = $dbc->query("SELECT UserID, SendEmail, SendSMS FROM Archiving.PolicyDataCustodians WHERE PolicyID={$this->id}")->fetch_all();
			$this->otherVersions = null;
			if (!$this->systemPolicy)
				$this->otherVersions = $this->_dbc->query(
						"SELECT IF(Version = {$this->version}, 'yes', 'no') AS `ThisVersion`, Version, UNIX_TIMESTAMP(ModifyTime) AS `ModifyTimeEpoch`, UserModifiedBy ".
						"FROM Archiving.PolicyVersions WHERE ID={$this->id} ORDER BY Version DESC")->fetch_all();

			if(!$this->storageEndpointID)
			{
				$endpoint = ArchivingEndpoint::Get($dbc, $this->org, $this->storageEndpointID);
				$this->storageEndpointID = $endpoint->id;
			}

			return true;
		}

		return false;
	}
	
	/**
	 * Update the data from API
	 * 
	 * @param array $api api details (url, auth)
	 * 
	 * @return bool
	 */
	protected function updateFromApi($api)
	{
		$url = $this->version? 
			sprintf('%s/archiving/policy/%d/%d/%d', $api[0], $this->org, $this->id, $this->version):
			sprintf('%s/archiving/policy/%d/%d', $api[0], $this->org, $this->id)
			;
		
		$stream = HTTPStream::Make('GET', $url, null, array('Authorization'=>$api[1]));
		$streamKiller = new ScopeSentry(
				function() use (&$stream)
				{
					if ($stream)
					{
						$stream = null;
						unset($stream);
					}
				});
		if ($stream && $stream->httpCode == 200)
		{
			$xpath = new XPath($stream->getContents());
			$policyNode = $xpath('/Archiving/Policy', true);
			if(!$policyNode)
				throw new Exception('Invalid response for Archiving Policy from API');
			$policyNode = $policyNode->item(0);

			$this->id = $xpath('ID', false, $policyNode);
			$this->name = $xpath('Name', false, $policyNode);
			$this->version = $xpath('Version', false, $policyNode);
			$this->retentionMin = array($xpath('RetentionMin', false, $policyNode), $xpath('RetentionMin/@Unit', false, $policyNode));
			$this->retentionMax = array($xpath('RetentionMax', false, $policyNode), $xpath('RetentionMax/@Unit', false, $policyNode));
			$this->reducedRedundancy = $xpath('ReducedRedundancy', false, $policyNode);
			$this->mode = $xpath('Mode', false, $policyNode);
			$this->storageEndpointID = $xpath('StorageEndpointID', false, $policyNode);
			$this->ownerUser = array('CanRead'=>$xpath('OwnerUser/@CanRead', false, $policyNode), 'CanDelete'=>$xpath('OwnerUser/@CanDelete', false, $policyNode));
			$this->askReasonForAccess = $xpath('AskReasonForAccess', false, $policyNode);
			$this->description = $xpath('Description', false, $policyNode);
			$this->systemPolicy = true; // TODO fix not differentiating readonly policy!
			$this->allowUnrestrictedAccess = $xpath('AllowUnrestrictedAccess', false, $policyNode);
			$this->disallowRecordingDownload = $xpath('DisallowRecordingDownload', false, $policyNode);

			if(!$this->storageEndpointID)
			{
				$endpoint = ArchivingEndpoint::Get($api, $this->org, $this->storageEndpointID, true);
				$this->storageEndpointID = $endpoint->id;
			}

			$this->users = array();
			if($nodes = $xpath('Users/User', true, $policyNode))
			{
				foreach($nodes as $node)
					$this->users[] = (object)array(
							'UserID'=>$xpath('@UserID', false, $node),
							'CanRead'=>$xpath('@CanRead', false, $node),
							'CanDelete'=>$xpath('@CanDelete', false, $node)
					);
			}

			$this->groups = array();
			if($nodes = $xpath('Groups/Group', true, $policyNode))
			{
				foreach($nodes as $node)
					$this->groups[] = (object)array(
							'GroupID'=>$xpath('@GroupID', false, $node),
							'CanRead'=>$xpath('@CanRead', false, $node),
							'CanDelete'=>$xpath('@CanDelete', false, $node)
					);
			}
				
			$this->notifyDataCustodians = $xpath('DataCustodians/@Notify', false, $policyNode);
			$this->dataCustodians = array();
			if($nodes = $xpath('DataCustodians/DataCustodian', true, $policyNode))
			{
				foreach($nodes as $node)
					$this->dataCustodians[] = (object)array(
							'UserID'=>$xpath('@UserID', false, $node),
							'SendEmail'=>$xpath('@SendEmail', false, $node),
							'SendSMS'=>$xpath('@SendSMS', false, $node)
					);
			}

			$this->otherVersions = null;
			if (!$this->systemPolicy)
			{
				$this->otherVersions = array();
				foreach($xpath('OtherVersions/OtherVersion', true, $policyNode) as $node)
					$this->dataCustodians[] = (object)array(
							'ThisVersion'=>$xpath('@ThisVersion', false, $node),
							'VersionNumber'=>$xpath('@VersionNumber', false, $node),
							'ModifyTimeEpoch'=>$xpath('@ModifyTimeEpoch', false, $node),
							'ModifiedByUserID'=>$xpath('@ModifiedByUserID', false, $node)
					);
			}
			
			return true;
		}
		
		return false;
	}

	/**
	 * Constructor
	 *
	 * @param dbc/api $dbc
	 * @param int     $rmOrgId
	 * @param int     $policyID
	 * @param int     $policyVersion
	 *
	 * @throws Exception
	 */
	function __construct($dbc, $rmOrgId, $policyID, $policyVersion=0)
	{
		$this->org = $rmOrgId;
		$this->id = $policyID;
		$this->version = $policyVersion;
		
		if (false === ($ret = is_array($dbc)? $this->updateFromApi($dbc): $this->updateFromDb($dbc)))
			throw new Exception('Archiving Policy not found');
	}
}
