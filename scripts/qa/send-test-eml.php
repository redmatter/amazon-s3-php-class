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
$domain = 'redmatter.com';
$sms_size = 100;
$mms_size = '600x100';
$mms_content_fname = null;
$mms_content_type = null;
$a = 1;
while ($a < $argc)
{
	if (!strcmp($argv[$a], '-smtp'))				$smtp = $argv[++$a];
	else if (!strcmp($argv[$a], '-from_number'))	$from_number = $argv[++$a];
	else if (!strcmp($argv[$a], '-to_number'))		$to_number = $argv[++$a];
	else if (!strcmp($argv[$a], '-sms'))			$mms = false;
	else if (!strcmp($argv[$a], '-mms'))			$mms = true;
	else if (!strcmp($argv[$a], '-c'))				$count = (int)$argv[++$a];
	else if (!strcmp($argv[$a], '-d'))				$domain = $argv[++$a];
	else if (!strcmp($argv[$a], '-ss'))				$sms_size = (int)$argv[++$a];
	else if (!strcmp($argv[$a], '-ms'))				$mms_size = $argv[++$a];
	else if (!strcmp($argv[$a], '-mc'))
	{
		$mms_content_fname = $argv[++$a];
		$mms_content_type = $argv[++$a];
	}
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
	echo "Use:\n";
	echo "  $app_name.php -smtp smtp_server -from_number sender_number -to_number to_number [-sms [-ss size]|-mms [-ms XSizexYSize]|[-mc File ContentType]] [-d Domain] [-c Count] [-v]\n";
	echo "E.g:\n";
	echo "  To send one SMS which contains the default size of $sms_size characters\n";
	echo "    $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -sms\n";
	echo "  To send 10 SMS messages on QA, each containing 200 characters\n";
	echo "    $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -sms -ss 200 -c 10 -d qa01.redmatter.com\n";
	echo "  To send one MMS message where the image is the default size of $mms_size\n";
	echo "    $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -mms -c 3 -v\n";
	echo "  To send 5 MMS messages where the image size is 1000x800\n";
	echo "    $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -mms -c 5 -ms 1000x800\n";
	echo "  To send 3 MMS messages where the content is that of file audio.mp3\n";
	echo "    $app_name.php -smtp londev07.redmatter.com -from_number 4792009479 -to_number 44792009478 -mms -c 3 -mc audio.mp3 audio/mpeg\n";
	echo "Command line options:\n";
	echo "  -smtp smtp_server           The SMTP server to send the request to\n";
	echo "  -from_number sender_number  Sender number\n";
	echo "  -to_number to_number        Destination number\n";
	echo "  -sms                        Send a sms, the default\n";
	echo "  -mms                        Send a mms\n";
	echo "  -c Count                    Number of messages to send, default to 1\n";
	echo "  -d Domain                   Domain to use, default is '$domain'\n";
	echo "  -ss size                    Number of characters for SMS message, default is $sms_size\n";
	echo "  -ms XSizexYSize             Image size for MMS messages, default is $mms_size\n";
	echo "  -mc File ContentSize        MMS content file and content type if build in image not required\n";
	echo "  -v                          Enable verbose\n";
	return 0;
}

// Info
if ($verbose)
{
	echo "Using following settings:\n";
	echo "  smtp             '$smtp'\n";
	echo "  from_number      '$from_number'\n";
	echo "  to_number        '$to_number'\n";
	echo "  message type     " . ($mms ? 'mms' : 'sms') . "\n";
	echo "  count            $count\n";
	echo "  domain           '$domain'\n";
	echo "  SMS size         $sms_size\n";
	echo "  MMS size         $mms_size\n";
	echo "  MMS content file '$mms_content_fname' '$mms_content_type'\n";
}

// If content file provided read content
$mms_content = null;
if ($mms_content_fname)
{
	if (($mms_content = file_get_contents($mms_content_fname)) === false)
		die("Unable to open '$mms_content_fname'\n");
}

// Set x & y values for image creation
$tile_xs = $tile_ys = 50;
$tile_im = null;
list($xs, $ys) = explode('x', $mms_size);
$xs = (int)($xs / $tile_xs) * $tile_xs;
$ys = (int)($ys / $tile_ys) * $tile_ys;
if ($xs < $tile_xs)
	$xs = $tile_xs;
if ($ys < $tile_ys)
	$ys = $tile_ys;

// Process all messages
for ($a=0; $a<$count; ++$a)
{
	// Construct eml data
	$from_mail = $app_name . '@' . $domain;
	$eml =	"From:<$from_number@$domain>\r\n" .
			"To: <$to_number@$domain>\r\n" .
			"Subject: $app_name $to_number\r\n" .
			"Date: " . date('r') . "\r\n" .
			"Message-ID: <" . rand(1000000, 9999999) . '.' . rand(10000, 99999) . '.' . rand(10000000, 99999999) . ".$app_name>\r\n" .
			"Content-Transfer-Encoding: base64\r\n" .
			"MIME-Version: 1.0\r\n";
	// String
	$str = 'This is test ' . ($mms ? 'MMS' : 'SMS') . ' message ' . ($a + 1) . '/' . $count . ' from '. getHostByName(php_uname('n')) . ' created on ' . date('r');

	// If SMS
	if (!$mms)
	{
		$eml .= "Content-Type: text/plain\r\n\r\n";
		$str2 = $str . ' ';
		$b = 0;
		while(strlen($str2) < $sms_size)
			$str2 .= $b++ % 10;
		$eml .= base64_encode($str2);
	}
	// Else MMS
	else
	{
		// If content provided
		if ($mms_content)
		{
			$path_parts = pathinfo($mms_content_fname);
			$eml .=	"Content-Type: $mms_content_type\r\n" .
					"Content-Disposition: filename=payload." . $path_parts['extension'] . ";\r\n" .
					"\r\n";
			$eml .= base64_encode($mms_content);
		}
		// Else
		else
		{
			// Construct .gif image with a noisy background (which is difficult for GIF to compress) and add some text
			$eml .=	"Content-Type: image/gif\r\n" .
					"Content-Disposition: filename=payload.gif;\r\n" .
					"\r\n";
			if (!$tile_im)
			{
				$tile_im = imagecreatetruecolor($tile_xs, $tile_ys);
				for ($y=0; $y<$tile_ys; ++$y)
				{
					for ($x=0; $x<$tile_xs; ++$x)
					{
						$d = rand(128, 255);
						imagesetpixel($tile_im, $x, $y, ($d << 16) | ($d << 8) | $d);
					}
				}
			}
			$im = imagecreatetruecolor($xs, $ys);
			for ($y=0; $y<$ys; $y+=$tile_ys)
			{
				for ($x=0; $x<$xs; $x+=$tile_xs)
					imagecopy($im, $tile_im, $x, $y, 0, 0, $tile_xs, $tile_ys);
			}
			imagefilledrectangle($im, 0, 10, $xs, 30, 0xc0c0ff);
			imagerectangle($im, 0, 10, $xs, 30, 0x00ff00);
			imagestring($im, 2, 2, 13, $str, 0x0000ff);
			imagerectangle($im, 0, 0, $xs - 1, $ys - 1, 0xff0000);
			// Output image to buffer
			ob_start();
			imagegif($im);
			imagedestroy($im);
			$data = ob_get_clean();
			$eml .= base64_encode($data);
			if (ob_get_length() > 0)
				ob_end_clean();
		}
	}
	// Create tmp file with eml data
	if (($tmp_fname = tempnam('/tmp', $app_name . '-')) === false)
		echo "Unable to obtain a temp filename\n";
	else {
		if ($verbose)
			echo "Creating file '$tmp_fname' which contains:\n  " . str_replace("\n", "\n  ", $eml) . "\n";
		if (file_put_contents($tmp_fname, $eml) === false)
			echo "Unable to create file '$tmp_fname'\n";
		else {
			// Send eml file using curl
			$cmd = 'curl --url "smtp://' . $smtp . '" --mail-from "' . $from_mail . '" --mail-rcpt "' . ($mms ? 'mms' : 'sms') . '@telia-capture.' . $domain . '" --upload-file ' . $tmp_fname . ' 2> /dev/null';
			if ($verbose)
				echo "Sending request using following cmd:\n  $cmd\n";
			if ($send)
				system($cmd);
			// Done with tmp file
			unlink($tmp_fname);
		}
	}
}

if ($verbose)
	echo "Complete\n";
