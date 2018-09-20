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
}
