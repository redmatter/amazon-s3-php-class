#!/usr/bin/php
<?php
//NoSniff
/**
 * Migration Scripts for RMFS -> Archiving
 *
 * PHP version 5
 *
 * @category  Files
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */


/*
Run this as below
'''''''''''''''''
php migration-pick-host.php | mysql -u root ArchivingInternal

Database schema
'''''''''''''''
CREATE TABLE IF NOT EXISTS `ArchivingInternal`.`Migration` (
  `UUID` binary(16) NOT NULL,
  `OrgID` int(10) unsigned NOT NULL,
  `Path` text COLLATE ascii_bin NOT NULL,
  `CreateEpoch` int(10) unsigned NOT NULL,
  `Type` enum('recordings') COLLATE ascii_bin NOT NULL,
  `MD5_s01fss01` binary(16) DEFAULT '0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `MD5_s01fss02` binary(16) DEFAULT '0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `MD5_gmtfss01` binary(16) DEFAULT '0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `MD5_gmtfss02` binary(16) DEFAULT '0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0',
  `Size_s01fss01` int(10) unsigned DEFAULT '0',
  `Size_s01fss02` int(10) unsigned DEFAULT '0',
  `Size_gmtfss01` int(10) unsigned DEFAULT '0',
  `Size_gmtfss02` int(10) unsigned DEFAULT '0',
  `GoodHost` enum('s01fss01','s01fss02','gmtfss01','gmtfss02') COLLATE ascii_bin DEFAULT NULL,
  `Status` enum('done','error','retry','pending') COLLATE ascii_bin NOT NULL DEFAULT 'pending',
  PRIMARY KEY (`UUID`)
) ENGINE=InnoDB DEFAULT CHARSET=ascii COLLATE=ascii_bin;

*/

$mysqli = new mysqli('localhost', 'root', null, 'ArchivingMigration');
if ($mysqli->connect_error)
	die('Connect Error ('.$mysqli->connect_errno.') '.$mysqli->connect_error);

$pick_result = array();

$sql = "SELECT ".
	"HEX(UUID) UUID, OrgID, Path, CreateEpoch, Type, ".
	"HEX(MD5_s01fss01) MD5_s01fss01, HEX(MD5_s01fss02) MD5_s01fss02, HEX(MD5_gmtfss01) MD5_gmtfss01, HEX(MD5_gmtfss02) MD5_gmtfss02, ".
	"Size_s01fss01, Size_s01fss02, Size_gmtfss01, Size_gmtfss02, GoodHost, Status ".
	"FROM Migration WHERE Status!='error' AND FileState!='error'";

if ($result = $mysqli->query($sql))
{
	while ($row = $result->fetch_object())
	{
		$md5_uniq = array_unique(array($row->MD5_s01fss01, $row->MD5_s01fss02, $row->MD5_gmtfss01, $row->MD5_gmtfss02));
		if (1 == count($md5_uniq) && $md5_uniq[0] == '00000000000000000000000000000000')
		{
			echo "-- UUID={$row->UUID} has all 00000000000000000000000000000000 ... well done!\n";
			echo "UPDATE Migration SET GoodHost=NULL, Status='error' WHERE UUID=0x{$row->UUID};\n";
			echo "-- \n";
			continue;
		}
		elseif (1 != count($md5_uniq))
		{
			$pick = array();
			if ($row->MD5_s01fss01 != '00000000000000000000000000000000')
				$pick['s01fss01'] = $row->MD5_s01fss01;
			if ($row->MD5_s01fss02 != '00000000000000000000000000000000')
				$pick['s01fss02'] = $row->MD5_s01fss02;
			if ($row->MD5_gmtfss01 != '00000000000000000000000000000000')
				$pick['gmtfss01'] = $row->MD5_gmtfss01;
			if ($row->MD5_gmtfss02 != '00000000000000000000000000000000')
				$pick['gmtfss02'] = $row->MD5_gmtfss02;
				
			// $pick has all MD5s that are valid, as a hash on hostname
			// $pick = array('host1'=>'md51', ...)
			// eg: Array ( [s01fss01] => fffff656bafffffffff0fd60ffff85ff [s01fss02] => 1824f656baff9801e700fd60cddb85a9 [gmtfss01] => 1824f656baff9801e700fd60cddb85a9 [gmtfss02] => 1824f656baff9801e700fd60cddb85a9 )
			
			$picked_host = false;
			
			$pick_hosts = array_keys($pick); // all hosts that we just picked
			$pick_hosts_count = count($pick_hosts); // its count
			if ($pick_hosts_count > 2)
			{
				// we need to find out how many of these MD5s are duplicating
				// after the formula below, we will have $pick_match_count = array('host1' => ItsMD5RepetetionCount, ...)
				// eg: Array ( [s01fss01] => 1 [s01fss02] => 3 [gmtfss01] => 3 [gmtfss02] => 3 )
				$pick_match_count = array();
				foreach ($pick_hosts as $host)
					$pick_match_count[$host] = array_reduce($pick_hosts, function($a, $b) use ($pick, $host) { $a += (($pick[$host] == $pick[$b]) ? 1 : 0); return $a; }, 0 );

				echo '-- $pick_match_count: ';
				echo str_replace(array("\r","\n","\t","     "), ' ', print_r($pick_match_count, true));
				echo "\n";
				
				// we now need to see how many combinations are there
				// since we have only 4 max combinations its a little simpler ... 5 would have been a mind f**k
				$dup_md5_counts = array_unique(array_values($pick_match_count));
				sort($dup_md5_counts, SORT_NUMERIC);
				
				echo '-- $dup_md5_counts: ';
				echo str_replace(array("\r","\n","\t","     "), ' ', print_r($dup_md5_counts, true));
				echo "\n";
				
				// so if we have only one set of MD5s
				if (1 == count($dup_md5_counts))
				{
					// and they are for all the "picked" hosts
					if ($dup_md5_counts[0] == $pick_hosts_count)
					{
						echo "-- UUID={$row->UUID} has only one MD5 to look at; picking a host randomly\n";
						$picked_host = $pick_hosts[rand(0, ($pick_hosts_count - 1))];
					}
					// else
					//TODO // we can pick the set with same MD5 and has biggest file size
				}
				elseif (2 == count($dup_md5_counts))
				{
					// we are relatively OK ... hopefully one of these counts is 1
					if (($dup_md5_counts[0] == 1) /* && ($dup_md5_counts[1] > ($pick_hosts_count/2))*/) // if only two variants and majority are same
					{
						$pickable_hosts = array();
						foreach ($pick_match_count as $host => $host_count)
						{
							if ($host_count == $dup_md5_counts[1])
								$pickable_hosts[] = $host;
						}
						
						echo "-- UUID={$row->UUID} picking one from hosts with duplicate MD5\n";
						$picked_host = $pickable_hosts[rand(0, (count($pickable_hosts) - 1))];
					}
					// else
					// TODO if $dup_md5_counts[0] != 1 // we have to rely on the biggest set and then the set with biggest file
											
					if (!isset($picked_host))
					{
						echo "-- UUID={$row->UUID} has 50 / 50 Crap ... thats De** Magic !!!\n";
						echo "UPDATE Migration SET GoodHost=NULL, Status='error' WHERE UUID=0x{$row->UUID};\n";
						echo "-- \n";
						continue;
					}
				}
			}
			// if we have only two; lets do a simpler comparison
			elseif ($pick_hosts_count == 2)
			{
				if ($pick[$pick_hosts[0]] == $pick[$pick_hosts[1]])
				{
					echo "-- UUID={$row->UUID} only two MD5s and they match\n";
					$picked_host = $pick_hosts[rand(0, 1)];
				}
			}
			// Oh Dear! only one copy to hand; lets hope thats the good one
			elseif ($pick_hosts_count == 1)
			{
				echo "-- UUID={$row->UUID} has only one MD5 from one host, to look at\n";
				$picked_host = $pick_hosts[0];
			}
			
			$Status = (false === $picked_host)? '"error"' : '"pending"';
			$GoodHost = (false === $picked_host)? 'NULL' : "'$picked_host'";
			$UUID = $row->UUID;
			
			echo "UPDATE Migration SET GoodHost=$GoodHost, Status=$Status WHERE UUID=0x$UUID;\n";
			echo "-- \n";
		}
	}
	$result->close(); 
} 

