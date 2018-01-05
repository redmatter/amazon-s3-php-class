#!/usr/bin/php
<?php

include 'vendor/autoload.php';
include 'rmapp/class.HTTPStream.php';
include 'rmapp/class.BaseApp.php';
include 'rmapp/class.API.php';
include 'restclient/RestClient.class.php';

use phpseclib\Net\SFTP;

define('APP_NAME_', 'phonero-cron');
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RMAPP_EXIT_ON_PHP_ERRORS', false);
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);
define('RMAPP_HAPI_OVERRIDES_ENABLED', true);
//define('RM_CLI_APP', true);

define('PHONERO_CONFIG_FILE', '/etc/archiving/phonero-cron.conf');

class UUID
{
    const FMT_NO_DASH = '%04x%04x%04x%04x%04x%04x%04x%04x';
    const FMT_WITH_DASH = '%04x%04x-%04x-%04x-%04x-%04x%04x%04x';

    const RGX_UUID = '/^(([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})|(([0-9a-f]{8})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{12})))$/';

    private static $_instance;

    private $_devrandom = null;

    /**
     * Ctor (private)
     */
    private function __construct()
    {
        if (is_null($this->_devrandom))
            $this->_devrandom = @fopen('/dev/urandom', 'r');
    }

    /**
     * Dtor
     */
    public function __destruct()
    {
        if (!is_null($this->_devrandom) && (false !== $this->_devrandom))
        {
            @fclose($this->_devrandom);
            $this->_devrandom = null;
        }
    }

    /**
     * Call static version
     *
     * Functions available
     * UUID::GetNew($noDashes=false)
     *
     * @param string $name name
     * @param array  $args args
     *
     * @throws Exception
     *
     * @return mixed
     */
    public static function __callStatic($name, $args)
    {
        if (!isset(self::$_instance))
            self::$_instance = new self();

        if (!method_exists(self::$_instance, "_$name"))
            throw new Exception("Unknown method $name");

        return call_user_func_array(array(self::$_instance, "_$name"), $args);
    }

    /**
     * the private helper for UUID::GetNew
     *
     * @param bool $noDashes no dashes?
     *
     * @return string
     */
    private function _GetNew($noDashes=false)
    {
        $fmt = $noDashes? self::FMT_NO_DASH: self::FMT_WITH_DASH;

        if ($this->_devrandom === false)
        {
            // slighly less secure Mersenne Twister based randomisation
            return sprintf(
                $fmt,
                mt_rand(0, 0xffff), mt_rand(0, 0xffff), mt_rand(0, 0xffff),
                mt_rand(0, 0x0fff) | 0x4000,
                mt_rand(0, 0x3fff) | 0x8000,
                mt_rand(0, 0xffff), mt_rand(0, 0xffff), mt_rand(0, 0xffff)
            );
        }
        else
        {
            // nice and secure
            $r = unpack('v*', fread($this->_devrandom, 16));
            return sprintf(
                $fmt,
                $r[1], $r[2], $r[3], $r[4] & 0x0fff | 0x4000,
                $r[5] & 0x3fff | 0x8000, $r[6], $r[7], $r[8]
            );
        }
    }
}

class MP3File
{
    protected $filename;
    public function __construct($filename)
    {
        $this->filename = $filename;
    }

    public static function formatTime($duration) //as hh:mm:ss
    {
        //return sprintf("%d:%02d", $duration/60, $duration%60);
        $hours = floor($duration / 3600);
        $minutes = floor( ($duration - ($hours * 3600)) / 60);
        $seconds = $duration - ($hours * 3600) - ($minutes * 60);
        return sprintf("%02d:%02d:%02d", $hours, $minutes, $seconds);
    }

    //Read first mp3 frame only...  use for CBR constant bit rate MP3s
    public function getDurationEstimate()
    {
        return $this->getDuration($use_cbr_estimate=true);
    }

    //Read entire file, frame by frame... ie: Variable Bit Rate (VBR)
    public function getDuration($use_cbr_estimate=false)
    {
        $fd = fopen($this->filename, "rb");

        $duration=0;
        $block = fread($fd, 100);
        $offset = $this->skipID3v2Tag($block);
        fseek($fd, $offset, SEEK_SET);
        while (!feof($fd))
        {
            $block = fread($fd, 10);
            if (strlen($block)<10) { break; }
            //looking for 1111 1111 111 (frame synchronization bits)
            else if ($block[0]=="\xff" && (ord($block[1])&0xe0) )
            {
                $info = self::parseFrameHeader(substr($block, 0, 4));
                if (empty($info['Framesize'])) { return $duration; } //some corrupt mp3 files
                fseek($fd, $info['Framesize']-10, SEEK_CUR);
                $duration += ( $info['Samples'] / $info['Sampling Rate'] );
            }
            else if (substr($block, 0, 3)=='TAG')
            {
                fseek($fd, 128-10, SEEK_CUR);//skip over id3v1 tag size
            }
            else
            {
                fseek($fd, -9, SEEK_CUR);
            }
            if ($use_cbr_estimate && !empty($info))
            {
                return $this->estimateDuration($info['Bitrate'],$offset);
            }
        }
        return round($duration);
    }

    private function estimateDuration($bitrate,$offset)
    {
        $kbps = ($bitrate*1000)/8;
        $datasize = filesize($this->filename) - $offset;
        return round($datasize / $kbps);
    }

    private function skipID3v2Tag(&$block)
    {
        if (substr($block, 0,3)=="ID3")
        {
            $id3v2_major_version = ord($block[3]);
            $id3v2_minor_version = ord($block[4]);
            $id3v2_flags = ord($block[5]);
            $flag_unsynchronisation  = $id3v2_flags & 0x80 ? 1 : 0;
            $flag_extended_header    = $id3v2_flags & 0x40 ? 1 : 0;
            $flag_experimental_ind   = $id3v2_flags & 0x20 ? 1 : 0;
            $flag_footer_present     = $id3v2_flags & 0x10 ? 1 : 0;
            $z0 = ord($block[6]);
            $z1 = ord($block[7]);
            $z2 = ord($block[8]);
            $z3 = ord($block[9]);
            if ( (($z0&0x80)==0) && (($z1&0x80)==0) && (($z2&0x80)==0) && (($z3&0x80)==0) )
            {
                $header_size = 10;
                $tag_size = (($z0&0x7f) * 2097152) + (($z1&0x7f) * 16384) + (($z2&0x7f) * 128) + ($z3&0x7f);
                $footer_size = $flag_footer_present ? 10 : 0;
                return $header_size + $tag_size + $footer_size;//bytes to skip
            }
        }
        return 0;
    }

    public static function parseFrameHeader($fourbytes)
    {
        static $versions = array(
            0x0=>'2.5',0x1=>'x',0x2=>'2',0x3=>'1', // x=>'reserved'
        );
        static $layers = array(
            0x0=>'x',0x1=>'3',0x2=>'2',0x3=>'1', // x=>'reserved'
        );
        static $bitrates = array(
            'V1L1'=>array(0,32,64,96,128,160,192,224,256,288,320,352,384,416,448),
            'V1L2'=>array(0,32,48,56, 64, 80, 96,112,128,160,192,224,256,320,384),
            'V1L3'=>array(0,32,40,48, 56, 64, 80, 96,112,128,160,192,224,256,320),
            'V2L1'=>array(0,32,48,56, 64, 80, 96,112,128,144,160,176,192,224,256),
            'V2L2'=>array(0, 8,16,24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160),
            'V2L3'=>array(0, 8,16,24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160),
        );
        static $sample_rates = array(
            '1'   => array(44100,48000,32000),
            '2'   => array(22050,24000,16000),
            '2.5' => array(11025,12000, 8000),
        );
        static $samples = array(
            1 => array( 1 => 384, 2 =>1152, 3 =>1152, ), //MPEGv1,     Layers 1,2,3
            2 => array( 1 => 384, 2 =>1152, 3 => 576, ), //MPEGv2/2.5, Layers 1,2,3
        );
        //$b0=ord($fourbytes[0]);//will always be 0xff
        $b1=ord($fourbytes[1]);
        $b2=ord($fourbytes[2]);
        $b3=ord($fourbytes[3]);

        $version_bits = ($b1 & 0x18) >> 3;
        $version = $versions[$version_bits];
        $simple_version =  ($version=='2.5' ? 2 : $version);

        $layer_bits = ($b1 & 0x06) >> 1;
        $layer = $layers[$layer_bits];

        $protection_bit = ($b1 & 0x01);
        $bitrate_key = sprintf('V%dL%d', $simple_version , $layer);
        $bitrate_idx = ($b2 & 0xf0) >> 4;
        $bitrate = isset($bitrates[$bitrate_key][$bitrate_idx]) ? $bitrates[$bitrate_key][$bitrate_idx] : 0;

        $sample_rate_idx = ($b2 & 0x0c) >> 2;//0xc => b1100
        $sample_rate = isset($sample_rates[$version][$sample_rate_idx]) ? $sample_rates[$version][$sample_rate_idx] : 0;
        $padding_bit = ($b2 & 0x02) >> 1;
        $private_bit = ($b2 & 0x01);
        $channel_mode_bits = ($b3 & 0xc0) >> 6;
        $mode_extension_bits = ($b3 & 0x30) >> 4;
        $copyright_bit = ($b3 & 0x08) >> 3;
        $original_bit = ($b3 & 0x04) >> 2;
        $emphasis = ($b3 & 0x03);

        $info = array();
        $info['Version'] = $version;//MPEGVersion
        $info['Layer'] = $layer;
        //$info['Protection Bit'] = $protection_bit; //0=> protected by 2 byte CRC, 1=>not protected
        $info['Bitrate'] = $bitrate;
        $info['Sampling Rate'] = $sample_rate;
        //$info['Padding Bit'] = $padding_bit;
        //$info['Private Bit'] = $private_bit;
        //$info['Channel Mode'] = $channel_mode_bits;
        //$info['Mode Extension'] = $mode_extension_bits;
        //$info['Copyright'] = $copyright_bit;
        //$info['Original'] = $original_bit;
        //$info['Emphasis'] = $emphasis;
        $info['Framesize'] = self::framesize($layer, $bitrate, $sample_rate, $padding_bit);
        $info['Samples'] = $samples[$simple_version][$layer];
        return $info;
    }

    private static function framesize($layer, $bitrate,$sample_rate,$padding_bit)
    {
        if ($layer==1)
            return intval(((12 * $bitrate*1000 /$sample_rate) + $padding_bit) * 4);
        else //layer 2, 3
            return intval(((144 * $bitrate*1000)/$sample_rate) + $padding_bit);
    }
}

class importPhonero extends BaseApp
{
    public $coreApiUrl = null;
    public $coreApiAuthCreds = null;
    public $api = null;

    private function log_php_error($msg)
    {
        $err = error_get_last();
        $text = "$msg: $err[message]";
        return $text;
    }

    private function sftpDownload($config)
    {
        LogMessage("Starting SFTP download from ".$config->host);

        // open the SFTP directory, copy files to local dir and delete (remotely) the ones we successfully got

        $sftp = new SFTP($config->host);

        if (!$sftp->login($config->username, $config->password))
            throw new Exception('SFTP Login failed: '.$config->host.": ".$sftp->getLastSFTPError());
        elseif (!$sftp->chdir($config->remote_dir))
            throw new Exception('SFTP error: could not CD to '.$config->remote_dir.": ".$sftp->getLastSFTPError());
        elseif (!$files = $sftp->nlist())
            throw new Exception('SFTP error: could not get file listing: '.$sftp->getLastSFTPError());
        elseif (!chdir($config->download_dir))
            throw new Exception(LogError('Could not CD locally to '.$config->download_dir));

        $count = 0;

        foreach($files as $file) {
            if (!preg_match($config->filename_regex, $file))
                LogVerbose("(Skipping file $file)");
            else {
                if (file_exists($file)) {
                    // if we are here, we previously downloaded the file, but it could
                    // not be deleted remotely. So delete it now.
                    LogMessage("File $file is already downloaded. Remotely deleting ...");
                    if (!$sftp->delete($file)) {
                        // Oh dear, we again failed to delete it
                        LogError("Failure remotely re-deleting file $file: ".$sftp->getLastSFTPError());
                    } elseif (!rename($file, $config->queue_dir . '/' . $file)) {
                        // hmm we failed again!
                        LogError($this->log_php_error("Error moving $file to {$config->queue_dir}/$file"));
                    } else {
                        LogMessage("File $file is queued for processing after successful retry to remotely delete");
                        ++$count;
                    }
                } elseif (!$sftp->get($file, $config->download_dir . '/' . $file)) {
                    LogMessage("File $file could not be downloaded. Deleting local copy, if any: ".$sftp->getLastSFTPError());
                    // delete it locally in case there is a remnant
                    if (file_exists($file) && !unlink($file))
                        LogError($this->log_php_error("Yikes: failure unlinking file $file"));
                } elseif (!$sftp->delete($file)) {
                    // if we are here the file downloaded correctly but the file could not be
                    // remotely deleted. Leave it here so we know to delete it next time
                    // round
                    LogError("Failure remotely deleting file $file: ".$sftp->getLastSFTPError());
                } else {
                    // ok it was downloaded correctly and remotely deleted. Move it to the
                    // queue directory
                    if (!rename($file, $config->queue_dir . '/' . $file))
                        LogError("Error moving $file to {$config->queue_dir}/$file for queueing");
                    else {
                        LogMessage("File $file is queued for processing");
                        ++$count;
                    }
                }
            }
        }

        // close the SFTP connection

        LogMessage("SFTP download process complete, $count new file(s) queued for processing");
        $sftp = null;
    }

    private function normalise($number)
    {
        if (strlen($number) == 8 &&
            substr($number, 0, 3) != '677' && // solomon islands
            substr($number, 0, 3) != '682') // cook islands
            return '47'.$number;
        else
            return $number;
    }

    private function insertUUIDDashes($uuid)
    {
        return  substr($uuid, 0, 8).'-'.
            substr($uuid, 8, 4).'-'.
            substr($uuid, 12, 4).'-'.
            substr($uuid, 16, 4).'-'.
            substr($uuid, 20);
    }

    private function processQueue($config)
    {
        if (!chdir($config->queue_dir))
            throw new Exception('Could not CD locally to '.$config->queue_dir);
        elseif (!($files = glob('*')))
            throw new Exception('Error scanning '.$config->queue_dir);

        if (sizeof($files) > 0)
        {
            $filexml = array();
            $fileinfo = array();

            $count = 0;

            foreach($files as $file) {

                $matches = array();
                if (preg_match('/^\+?([0-9]+)_\+?([0-9]+)_\+?([0-9]+)_([0-9]+)_([0-9]{4}-[0-9]{2}-[0-9]{2})_([0-9]{6})\.mp3$/', $file, $matches) == false)
                    LogWarning("File doesn't match phonero regex: $file");
                else {
                    $mp3data = new MP3File($file);
                    LogMessage("$file length: {$mp3data->getDuration()} seconds");

                    $TimeStr = sprintf("%s %s:%s:%s",
                        $matches[5],
                        substr($matches[6], 0, 2),
                        substr($matches[6], 2, 2),
                        substr($matches[6], 4, 2)
                    );
                    $epoch = date_create($TimeStr, new DateTimeZone("Europe/Oslo"))->setTimezone(new DateTimeZone("UTC"))->format("U");

                    $auuid = UUID::GetNew(true);
                    $buuid = UUID::GetNew(true);
                    $duration = $mp3data->getDuration();

                    // $matches[1] is owner, [2] is A number, [3] is B number
                    // we can determine whether it's MO or MT this way. The owner number
                    // will always be local norwegian format with 0 removed, so just add 47.
                    if ($matches[1] == $matches[2]) {
                        $fromnumber = '47' . $matches[2];
                        $tonumber = $this->normalise($matches[3]);
                    }
                    elseif ($matches[1] == $matches[3]) {
                        $fromnumber = $this->normalise($matches[2]);
                        $tonumber = '47' . $matches[3];
                    }
                    else {
                        LogWarning("Owner $matches[1] doesn't match A $matches[2] or B $matches[3] numbers, can't handle this call");
                        continue;
                    }

                    $filexml[] = "<ImportedCall><AUUID>$auuid</AUUID><BUUID>$buuid</BUUID><Time>$epoch</Time><From_Number>$fromnumber</From_Number><To_Number>$tonumber</To_Number><Time_Talking>$duration</Time_Talking></ImportedCall>";

                    $fileinfo[] = array('Name' => $file, 'AUUID' => $auuid, 'BUUID' => $buuid, 'Duration' => $duration, 'Epoch' => $epoch, 'From_Number' => $fromnumber, 'To_Number' => $tonumber);

                    $count++;
                }

                if ($count > $config->hi_water_mark) {
                    LogMessage ("$count messages in the queue: that'll do for now");
                    break;
                }
            }

            $logResults = array();

            if (sizeof($filexml))
            {
                $xml = '<?xml version="1.0" encoding="UTF-8"?><ImportedCalls>' . implode("", $filexml) . '</ImportedCalls>';
                LogMessage($xml);

                // authenticate onto API

                API::Init($config->coreapi_url, new ArrayStorage(), APP_NAME_ . ':1.0');
                API::LoginBasicAuth($config->coreapi_creds, null/*mfa*/, true/*re_authenticate*/);

                LogDebug("Got auth CoreAPI token: " . API::GetToken());

                $authSentry = new ScopeSentry(
                    function () {
                        API::LogOut();
                        LogDebug("Logged out of API");
                    }
                );

                // make the API logs call

                $client = RestClient::createClient();
                $client->setUrl($config->coreapi_url . '/logs');
                $client->setMethod('POST');
                $client->setHeaders(array(
                    'Authorization' => API::GetToken(),
                    'Content-Type' => 'application/xml'
                ));
                $client->setParameters($xml);
                $client->execute();

                $logsResponse = trim($client->getResponse());

                if (empty($logsResponse))
                    throw new Exception("Failed to fetch logs from API");
                else
                    LogMessage("Log API response: $logsResponse");

                if (!$xml = @simplexml_load_string(trim($logsResponse)))
                    throw new Exception("Invalid XML in api /logs response");

                if ($xml->getName() != 'ImportedAudioLogs')
                    throw new Exception("Invalid root element in api /logs response: " . $xml->getName());

                foreach ($xml as $name => $el) {
                    if ($name != 'ImportedAudioLog')
                        throw new Exception("Invalid inner element name in api /logs response: {$name}");
                    else
                        $logResults[(string)$el['UUID']] = array('OrgID' => $el['OrgID'], 'UserID' => $el['UserID'], 'Direction' => $el['Direction']);
                }
            }

            foreach($fileinfo as $finfo)
            {
                $foundA = key_exists($finfo['AUUID'], $logResults);
                $foundB = key_exists($finfo['BUUID'], $logResults);
                $copyops = array();

                if ($foundA)
                    $fnameA = $this->insertUUIDDashes($finfo['AUUID'])."_{$logResults[$finfo['AUUID']]['OrgID']}_{$logResults[$finfo['AUUID']]['UserID']}_{$finfo['Epoch']}_phonero.mp3";
                if ($foundB)
                    $fnameB = $this->insertUUIDDashes($finfo['BUUID'])."_{$logResults[$finfo['BUUID']]['OrgID']}_{$logResults[$finfo['BUUID']]['UserID']}_{$finfo['Epoch']}_phonero.mp3";

                if ($foundA) {
                    $copyops[] = array('op' => 'rename', 'src' => $finfo['Name'], 'dst' => $config->import_dir . '/' . $fnameA);
                    if ($foundB)
                        $copyops[] = array('op' => 'copy', 'src' => $config->import_dir . '/' . $fnameA, 'dst' => $config->import_dir . '/' . $fnameB);
                }
                else if ($foundB)
                    $copyops[] = array('op' => 'rename', 'src' => $finfo['Name'], 'dst' => $config->import_dir . '/' . $fnameB);
                else
                    $copyops[] = array('op' => 'rename', 'src' => $finfo['Name'], 'dst' => $config->not_handled_dir . '/' . $finfo['Name']);

                foreach($copyops as $op) {
                    $res = $op['op']($op['src'], $op['dst']);
                    if (!$res)
                        LogError($this->log_php_error("$op[op]: $op[src] -> $op[dst]"));
                    else
                        LogMessage("$op[op]: $op[src] -> $op[dst] OK");
                }
            }
        }
    }

    public function run()
    {
        // load config

        LogMessage("Starting Phonero import task");

        // check config has all items we need

        $mandatory_config_items = array(
            'host',
            'username',
            'password',
            'filename_regex',
            'remote_dir',
            'download_dir',
            'queue_dir',
            'not_handled_dir',
            'import_dir',
            'coreapi_url',
            'coreapi_creds',
            'hi_water_mark'
        );

        $config = array();

        foreach ($mandatory_config_items as $item) {
            $val = $this->conf->get("phonero.$item", false);
            if (!$val)
                throw new Exception("Config item phonero.$item missing from ".PHONERO_CONFIG_FILE);

            $config[$item] = $val;
        }

        $config = (object) $config;

        // get hAPI stuff and transform config if necessary

        $hAPIHost = new HAPIHost();
        $this->hAPISearchDomain = $hAPIHost->get('SearchDomain');
        $this->hAPILocName = $hAPIHost->get('LocName');
        $config->coreapi_url = str_replace('{hAPI:SearchDomain}', $this->hAPISearchDomain, $config->coreapi_url);
        $config->import_dir .= '/'.$this->hAPILocName;

        // set uid and gid to cdrmunch

        if (!is_array($userinfo = posix_getpwnam("cdrmunch")))
            throw new Exception("posix_getpwnam(cdrmunch) failed: ".posix_strerror(posix_get_last_error()));
        elseif (!posix_setgid($userinfo['gid']))
            throw new Exception("posix_setgid($userinfo[gid]) failed: ".posix_strerror(posix_get_last_error()));
        elseif (!posix_setuid($userinfo['uid']))
            throw new Exception("posix_setuid($userinfo[uid]) failed: ".posix_strerror(posix_get_last_error()));
        LogDebug("Running as cdrmunch");

        // create import directory with LocName if it doesn't exist

        if (!file_exists ($config->import_dir)) {
            if (!mkdir ($config->import_dir, 0777, false))
                throw new Exception($this->log_php_error("mkdir($config->import_dir) failed"));
            else
                LogMessage("mkdir($config->import_dir) succeeded");
        }

        // download any new audio files from the SFTP server. This is a self contained process

        LogMessage("Downloading phonero files via SFTP");

        try
        {
            $this->sftpDownload($config);
        }
        catch (Exception $e)
        {
            LogError("Exception while processing SFTP download: ".$e->getMessage());
        }

        // process items in the queue directory and place successful ones in the import directory

        LogMessage("Processing queued phonero files");

        try
        {
            $this->processQueue($config);
        }
        catch (Exception $e)
        {
            LogError("Exception while processing queue: ".$e->getMessage());
        }

        LogMessage("Finished");
    }
}

$app = importPhonero::getInstance(PHONERO_CONFIG_FILE);
$app->run();
