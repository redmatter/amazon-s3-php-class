<?php
/**
 * CoreAPI.php
 *
 * PHP version 5
 *
 * @category  CoreAPI.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Services;

use APIAuthRequiredException;
use EventLog;

class CoreAPI
{
    /**
     * @param $userId
     *
     * @return object
     */
    public static function GetUserDetails($userId)
    {
        $xpath = \API::Get("/users/{$userId}");

        $firstName = $xpath('/Users/User/@FirstName');
        $middleNames = $xpath('/Users/User/@MiddleNames');
        $lastName = $xpath('/Users/User/@LastName');
        $middleNames = $middleNames ? "{$middleNames} " : '';
        $name = "{$firstName} {$middleNames}{$lastName}";

        return (object)[
            'ID'            => $userId,
            'OrgID'         => $xpath('/Users/User/@HomeOrgID'),
            'UserName'      => $xpath('/Users/User/@UserName'),
            'FirstName'     => $firstName,
            'MiddleNames'   => $middleNames,
            'LastName'      => $lastName,
            'Name'          => $name,
            'PrimaryMobile' => $xpath('/Users/User/@PrimaryMobile'),
        ];
    }

    /**
     * Create event-log entry
     *
     * @param string $level
     * @param int    $orgId
     * @param string $context
     * @param string      $action
     * @param string    $message
     * @param int    $userId
     * @param int    $deviceId
     * @param null   $extraData
     * @param null   $authToken
     * @param bool   $isAsync
     *
     * @throws APIAuthRequiredException
     * @return EventLog
     */
    public static function CreateEventLog($level, $orgId, $context, $action, $message, $userId = 0, $deviceId = 0, $extraData = null, $authToken = null, $isAsync = false)
    {
        $auth = \API::GetToken(true);
        EventLog::Configure($auth->URL);
        EventLog::SetAuthorization($auth->Token);

        /** @noinspection VariableFunctionsUsageInspection we still have to live with 5.3 */
        return call_user_func(
            array('EventLog', $level), $orgId, $context, $action, $message,
            $userId, $deviceId, $extraData, $authToken, $isAsync);
    }
}
