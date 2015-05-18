#!/usr/bin/php
<?php

/**
 * Utility to send test eml files containing SMS or MMS to the eml2-milter process which then adds the entry to archiving
 *
 * PHP version 5
 *
 * @category  QAScripts
 * @package   ArchivingQAScripts
 * @author    James Bravo <james.bravo@redmatter.com>
 * @copyright 2015 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: $Id: $
 * @link      http://www.redmatter.com
 */

// Reduce warnings
error_reporting(~E_WARNING);

// Process cmd line args
$app_name = str_replace('./', '', str_replace('.php', '', $argv[0]));
$smtp = $from_number = $to_number = null;
$mms = $verbose = $help = false;
$send = true;
$count = 1;
$a = 1;
while ($a < $argc)
{
	if (!strcmp($argv[$a], '-smtp'))				$smtp = $argv[++$a];
	else if (!strcmp($argv[$a], '-from_number'))	$from_number = $argv[++$a];
	else if (!strcmp($argv[$a], '-to_number'))		$to_number = $argv[++$a];
	else if (!strcmp($argv[$a], '-sms'))			$mms = false;
	else if (!strcmp($argv[$a], '-mms'))			$mms = true;
	else if (!strcmp($argv[$a], '-c'))				$count = (int)$argv[++$a];
	else if (!strcmp($argv[$a], '-v'))				$verbose = true;
	else if (!strcmp($argv[$a], '-nosend'))			$send = false;
	else
		$help = true;
	++$a;
}
if (!$smtp || !$from_number || !$to_number)
	$help = true;
if ($help)
{
	echo "$app_name.php (c) Redmatter 2015\n";
	echo "Utility to send test eml files containing SMS or MMS to the eml2-milter process which then adds the entry to archiving\n";
	echo "Use :- $app_name.php -smtp smtp_server -from_number sender_number -to_number to_number [-sms|-mms] [-c Count] [-v]\n";
	echo "E.g :- $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478\n";
	echo "E.g :- $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -mms -c 3 -v\n";
	echo "-smtp         smtp server to send the request to\n";
	echo "-from_number  sender number\n";
	echo "-sms          send a sms, the default\n";
	echo "-mms          send a mms\n";
	echo "-c            number of messages to send, default to 1\n";
	echo "-v            enable verbose\n";
	return 0;
}

// Info
if ($verbose)
{
	echo "Using following settings:\n";
	echo "  smtp           '$smtp'\n";
	echo "  from_number    '$from_number'\n";
	echo "  to_number      '$to_number'\n";
	echo "  message type   " . ($mms ? 'mms' : 'sms') . "\n";
	echo "  count          $count\n";
}

// Process all messages
for ($a=0; $a<$count; ++$a)
{
	// Construct eml data
	$domain = 'redmatter.com';
	$from_mail = $app_name . '@redmatter.com';
	$eml =	"From:<$from_number@$domain>\r\n" .
			"To: <$to_number@$domain>\r\n" .
			"Subject: $app_name $to_number\r\n" .
			"Date: " . date('r') . "\r\n" .
			"Message-ID: <" . rand(1000000, 9999999) . '.' . rand(10000, 99999) . '.' . rand(10000000, 99999999) . ".$app_name>\r\n" .
			"Content-Transfer-Encoding: base64\r\n" .
			"MIME-Version: 1.0\r\n";

	// If SMS
	if (!$mms)
	{
		$eml .= "Content-Type: text/plain\r\n\r\n";
		$eml .= base64_encode('This is test ' . ($mms ? 'MMS' : 'SMS') . ' message ' . ($a + 1) . '/' . $count . ' from '. getHostByName(php_uname('n')) . ' created on ' . date('r'));
	}
	// Else MMS so construct .gif image with text
	else {
		$eml .= "Content-Type: image/gif\r\n\r\n";
		ob_start();
		$im = imagecreatetruecolor(100, 100);
		imagefilledrectangle($im, 0, 0, 99, 99, 0xffffff);
		imagerectangle($im, 0, 0, 99, 99, 0xff0000);
		imagestring($im, 1, 2, 20, 'This is a test', 0x0000ff);
		imagestring($im, 1, 2, 30, 'MMS message', 0x0000ff);
		imagestring($im, 1, 2, 40, 'from '. getHostByName(php_uname('n')), 0x0000ff);
		imagestring($im, 1, 2, 50, 'created on', 0x0000ff);
		imagestring($im, 1, 2, 60, date('d-m-o H:i:s O'), 0x0000ff);
		imagegif($im);
		imagedestroy($im);
		$data = ob_get_clean();
		$eml .= base64_encode($data);
		if (ob_get_length() > 0)
			ob_end_clean();
	}
	// Create tmp file with eml data
	if (($tmp_fname = tempnam('/tmp', $app_name . '-')) === false)
		echo "Unable to obtain a temp filename\n";
	else {
		$tmp_fname .= '.tmp';
		if ($verbose)
			echo "Creating file '$tmp_fname' which contains:\n  " . str_replace("\n", "\n  ", $eml) . "\n";
		if (file_put_contents($tmp_fname, $eml) === false)
			echo "Unable to create file '$tmp_fname'\n";
		else {
			// Send eml file using curl
			$cmd = 'curl --url "smtp://' . $smtp . '" --mail-from "' . $from_mail . '" --mail-rcpt "sms@telia-capture.redmatter.com" --upload-file ' . $tmp_fname . ' 2> /dev/null';
			if ($verbose)
				echo "Sending request using:\n  $cmd\n";
			if ($send)
				system($cmd);
			// Done with tmp file
			unlink($tmp_fname);
		}
	}
}

if ($verbose)
	echo "Complete\n";
