<?php
/**
 * StorageEndpointCredentials.php
 *
 * PHP version 5
 *
 * @category  StorageEndpointCredentials.php
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2015 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

namespace RedMatter\Archiving\AWS;

use Aws\Common\Credentials\CredentialsInterface;

/**
 * Class StorageEndpointCredentials
 *
 * @category  Library
 * @package   RedMatter\Archiving\AWS
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class StorageEndpointCredentials implements CredentialsInterface
{
    private $endpoint;

    /**
     * wraps the CredentialsInterface around endpoint
     *
     * @param \ArchivingEndpoint $ep storage endpoint to wrap
     */
    public function __construct(\ArchivingEndpoint $ep)
    {
        $this->endpoint = $ep;
    }

    /**
     * (PHP 5 &gt;= 5.1.0)<br/>
     * String representation of object
     *
     * @link http://php.net/manual/en/serializable.serialize.php
     * @return string the string representation of the object or null
     */
    public function serialize()
    {
        return null;
    }

    /**
     * (PHP 5 &gt;= 5.1.0)<br/>
     * Constructs the object
     *
     * @param string $serialized The string representation of the object.
     *
     * @link http://php.net/manual/en/serializable.unserialize.php
     *
     * @return void
     */
    public function unserialize($serialized)
    {
    }

    /**
     * Returns the AWS access key ID for this credentials object.
     *
     * @return string
     */
    public function getAccessKeyId()
    {
        return $this->endpoint->accessKey;
    }

    /**
     * Returns the AWS secret access key for this credentials object.
     *
     * @return string
     */
    public function getSecretKey()
    {
        return $this->endpoint->secretKey;
    }

    /**
     * Get the associated security token if available
     *
     * @return string|null
     */
    public function getSecurityToken()
    {
        return null;
    }

    /**
     * Get the UNIX timestamp in which the credentials will expire
     *
     * @return int|null
     */
    public function getExpiration()
    {
        return null;
    }

    /**
     * Set the AWS access key ID for this credentials object.
     *
     * @param string $key AWS access key ID
     *
     * @return self
     */
    public function setAccessKeyId($key)
    {
        return $this;
    }

    /**
     * Set the AWS secret access key for this credentials object.
     *
     * @param string $secret AWS secret access key
     *
     * @return CredentialsInterface
     */
    public function setSecretKey($secret)
    {
        return $this;
    }

    /**
     * Set the security token to use with this credentials object
     *
     * @param string $token Security token
     *
     * @return self
     */
    public function setSecurityToken($token)
    {
        return $this;
    }

    /**
     * Set the UNIX timestamp in which the credentials will expire
     *
     * @param int $timestamp UNIX timestamp expiration
     *
     * @return self
     */
    public function setExpiration($timestamp)
    {
        return $this;
    }

    /**
     * Check if the credentials are expired
     *
     * @return bool
     */
    public function isExpired()
    {
        return false;
    }
}
