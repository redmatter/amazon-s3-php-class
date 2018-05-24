<?php
/**
 * DGAPI
 *
 * PHP version 5
 *
 * @category  Files
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
 * Request
 *
 * @category  Library
 * @package   RedMatter_Request
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Services;

class DGAPI
{
    private $_baseUrl;
    private $_authToken;

    /**
     * DGAPI constructor.
     *
     * @param $baseUrl
     * @param $authToken
     */
    public function __construct($baseUrl, $authToken)
    {
        $this->_baseUrl = $baseUrl;
        $this->_authToken = $authToken;
    }

    /**
     * Send the given XML as it is, to the specified URI
     *
     * @param string $uri uri
     * @param string $xml xml
     */
    public function RawSend($uri, $xml)
    {
        $this->_send($uri, $xml);
    }

    /**
     * Send it
     *
     * @param string $uri uri
     * @param string $xml xml
     *
     * @throws \RuntimeException
     */
    private function _send($uri, $xml)
    {
        $stream = \HTTPStream::Make('PUT', "{$this->_baseUrl}{$uri}", $xml,
            array('Authorization' => $this->_authToken, 'Content-Type' => 'application/xml'));
        if ($stream && !$stream->httpCode) {
            throw new \RuntimeException('Connection failed to DGAPI');
        }

        $responseCode = (int)$stream->httpCode;

        if (($responseCode < 200) || ($responseCode > 300)) {
            throw new \RuntimeException('Request to DGAPI failed');
        }
    }

    /**
     * SendSMS
     *
     * @param int    $orgID      org
     * @param array  $recipients array(array('Name1', 'MobileNum1'), array('Name2', 'MobileNum2') ...)
     * @param string $text       sms
     * @param string $fromNumber from name
     *
     * @throws \Exception
     */
    public function SendSMS($orgID, $recipients, $text, $fromNumber = null)
    {
        foreach ($recipients as $recipient) {
            $dom = new \DOMMaker('sms');
            if ($fromNumber) {
                $dom->nodeCreate('FromNumber', $fromNumber);
            }

            $dom->nodeCreate('ToNumber', $recipient[1]);
            $dom->nodeCreate('rmOrgID', $orgID);
            $dom->nodeCreate('SMSText',
                \Template::Expand($text, array('ToName' => $recipient[0], 'ToNumber' => $recipient[1])));

            $this->_send('/sms', $dom->getDOMDocument()->saveXML());
        }
    }

    /**
     * Send Templated Email
     *
     * @param mixed  $tplDef     EmailTemplate / Array(TemplateTheme,EmailTemplate)
     * @param int    $orgID      Org
     * @param array  $recipients array(ToName, ToAddress) / array(array(ToName1, ToAddress1), array(ToName2,
     *                           ToAddress2), ...)
     * @param string $subject    Subject
     * @param array  $vars       array(Var1 => Val1, Var2 => Val2, ...)
     * @param array  $from       Null / array(FromName, FromAddress)
     * @param mixed  $cc         Null / array(ccAddress1, ccAddress2, ... )
     *
     * @throws \Exception
     */
    public function SendTemplateEmail($tplDef, $orgID, $recipients, $subject, $vars, $from = null, $cc = null)
    {
        if ($cc && is_array($cc)) {
            $cc = implode(',', $cc);
        }

        if (!is_array($tplDef)) {
            $tplDef = array('natterbox', $tplDef);
        }

        foreach ($recipients as $recipient) {
            $dom = new \DOMMaker('email');
            $dom->nodeCreate('rmOrgID', $orgID);
            $dom->nodeCreate('Subject', $subject);
            // $dom->nodeCreate('TemplateTheme', $tplDef[0]);
            $dom->nodeCreate('EmailTemplate', $tplDef[1]);
            $dom->nodeCreate('FromName', $from[0]);
            $dom->nodeCreate('FromAddress', $from[1]);

            if ($cc) {
                $dom->nodeCreate('CcAddress', $cc);
            }

            foreach ($vars as $varName => $varValue) {
                $dom->nodeCreate($varName,
                    \Template::Expand($varValue, array('ToName' => $recipient[0], 'ToAddress' => $recipient[1])));
            }

            $dom->nodeCreate('ToName', $recipient[0]);
            $dom->nodeCreate('ToAddress', $recipient[1]);

            $this->_send('/email', $dom->getDOMDocument()->saveXML());
        }
    }

    /**
     * Send Email using the Blank Template
     *
     * @param int    $orgID      Org
     * @param array  $recipients array(ToName, ToAddress) / array(array(ToName1, ToAddress1), array(ToName2,
     *                           ToAddress2), ...)
     * @param string $subject    Subject
     * @param mixed  $body       array("Text" => "Message; Text formatted", HTML => "Message; HTML formatted") / Text
     *                           Message (string)
     * @param string $heading    heading
     * @param array  $from       Null / array(FromName, FromAddress)
     * @param mixed  $cc         Null / array(ccAddress1, ccAddress2, ... )
     * @param bool   $htmlOnly   html only?
     *
     * @throws \Exception
     */
    public function SendEmail(
        $orgID,
        $recipients,
        $subject,
        $body,
        $heading = '&nbsp;',
        $from = null,
        $cc = null,
        $htmlOnly = false
    ) {
        if (!is_array($body)) {
            /** @noinspection NotOptimalRegularExpressionsInspection */
            $body = $htmlOnly ?
                array(
                    'Text' => 'Please use an HTML compatible email client to view this message.',
                    'HTML' => $body
                ) :
                array(
                    'Text' => $body,
                    'HTML' => nl2br(preg_replace(
                        '#(^|[^\"=]{1})(https://|http://|ftp://|mailto:|news:)([^\s<>]+)([\s\n<>]|$)#sm',
                        "\\1<a href=\"\\2\\3\">\\2\\3</a>\\4", $body))
                );
        }

        $this->SendTemplateEmail('Blank', $orgID, $recipients, $subject,
            array('MessageText' => $body['Text'], 'MessageHTML' => $body['HTML'], 'Heading' => $heading), $from, $cc);
    }
}
