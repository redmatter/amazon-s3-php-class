<?php
/**
 * Notification.php
 *
 * PHP version 5
 *
 * @category  Notification.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2018 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\DataSubjectRequest;

class Notification
{
    // a fresh request created - notify custodian(s)
    const TYPE_STR_CREATED_CUSTODIAN = 'created:custodian';
    const TYPE_CREATED_CUSTODIAN = 101;
    // a fresh request created - acknowledge the requester
    const TYPE_STR_CREATED_REQUESTER = 'created:requester';
    const TYPE_CREATED_REQUESTER = 201;

    // a request still waiting for approval - notify custodian(s)
    const TYPE_STR_PENDING_CUSTODIAN = 'pending:custodian';
    const TYPE_PENDING_CUSTODIAN = 102;

    // in progress - inform the requester
    const TYPE_STR_PROGRESS_REQUESTER = 'progress:requester';
    const TYPE_PROGRESS_REQUESTER = 203;

    // we made it - inform the requester
    const TYPE_STR_DONE_REQUESTER = 'done:requester';
    const TYPE_DONE_REQUESTER = 204;

    // we might not make it - inform the requester (only delete-request)
    const TYPE_STR_DELAYED_REQUESTER = 'delayed:requester';
    const TYPE_DELAYED_REQUESTER = 205;

    // about to expire - inform the requester (only legal-hold)
    const TYPE_STR_EXPIRY_REQUESTER = 'expiry:requester';
    const TYPE_EXPIRY_REQUESTER = 206;

    private static $IntMap = null;
    private $type;
    private $sentTime = null;

    /**
     * @return array
     */
    private static function GetMap()
    {
        if (self::$IntMap === null)
        {
            self::$IntMap = [
                self::TYPE_STR_CREATED_CUSTODIAN => self::TYPE_CREATED_CUSTODIAN,
                self::TYPE_STR_CREATED_REQUESTER => self::TYPE_CREATED_REQUESTER,

                self::TYPE_STR_PENDING_CUSTODIAN => self::TYPE_PENDING_CUSTODIAN,

                self::TYPE_STR_PROGRESS_REQUESTER => self::TYPE_PROGRESS_REQUESTER,

                self::TYPE_STR_DONE_REQUESTER => self::TYPE_DONE_REQUESTER,

                self::TYPE_STR_DELAYED_REQUESTER => self::TYPE_DELAYED_REQUESTER,

                self::TYPE_STR_EXPIRY_REQUESTER => self::TYPE_EXPIRY_REQUESTER,
            ];
        }

        return self::$IntMap;
    }

    /**
     * @param int $dsrType
     *
     * @return array
     * @throws \LogicException
     */
    public static function GetTypes($dsrType)
    {
        switch ($dsrType)
        {
            case DsrType::DELETE_REQUEST:
                return array_diff_key(self::GetMap(), [self::TYPE_STR_EXPIRY_REQUESTER => 0]);
            case DsrType::LEGAL_HOLD:
                return array_diff_key(self::GetMap(), [self::TYPE_STR_DELAYED_REQUESTER => 0]);
            default:
                throw new \LogicException("Unknown dsr-type: {$dsrType}");
        }
    }

    /**
     * @param int $dsrType
     *
     * @return string
     */
    public static function GetTypesSqlUnion($dsrType)
    {
        return implode(' UNION ', array_map(function ($type) {
            return "SELECT {$type} Type";
        }, self::GetTypes($dsrType)));
    }

    public function __construct($type, $sentTime)
    {
        $this->type = $type;
        if ($sentTime !== null)
        {
            $this->sentTime = new \DateTime("@{$sentTime}", new \DateTimeZone('UTC'));
        }
    }

    /**
     * @return mixed
     */
    public function getType()
    {
        return $this->type;
    }

    /**
     * @return \DateTime|null
     */
    public function getSentTime()
    {
        return $this->sentTime;
    }

    /**
     * @return bool
     */
    public function isSent()
    {
        return null !== $this->sentTime;
    }

    /**
     * Get number of days since the notification
     *
     * @return int|null
     */
    public function getDaysSince()
    {
        if ($this->sentTime)
        {
            $now = new \DateTime('now', new \DateTimeZone('UTC'));
            return $now->diff($this->sentTime)->days;
        }

        return null;
    }
}
