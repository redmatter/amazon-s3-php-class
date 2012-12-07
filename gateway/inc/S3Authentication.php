<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway
{

use \fluid\Client;

use rmarchgateway\AuthDataConfigAccessor;

/**
 * Archiving Gateway
 * Utility class used to encapsulate the functionality to authenticate data.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class S3Authentication
{	
	protected $method;
	protected $params;
	protected $queryString;
	protected $headers;
	
	/**
	 * Constructor
	 * 
	 * @param string             $method      HTTP 'verb' for the request (e.g. GET, POST, etc).
	 * @param array              $params      URI path elements specified in the client HTTP request.
	 * @param \fluid\QueryString $queryString Contains the query element of the client HTTP request URI (after the ?).
	 * @param array              $headers     Contains all headers from the client HTTP request.
	 * 
	 * @return	none
	 */
	public function __construct($method, $params, $queryString, $headers)
	{
		$this->method = $method;
		$this->params = $params;
		$this->queryString = $queryString;
		$this->headers = $headers;
	}
	
	/**
	 * isAuthenticated
	 * Determines whether or not the Authorization HTTP parameter has been populated
	 * with an 'AWS' prefixed string and returns true if so.
	 *
	 * @return	bool
	 */
	public function isAuthenticated()
	{
		$authentication_info = $this->getAuthenticationInfo(); 
		if (empty($authentication_info) === true)
		{
			return false;
		}
		else
		{
			return true;
		}
	}
	
	/**
	 * checkAuthentication
	 * Authenticates the HTTP request according to AWS documentation.
	 *
	 * @return	bool
	 */
	public function checkAuthentication()
	{
		// Simply return the combined result of these functions, which verify that the date supplied is within
		// the appropriate range and that the signature is correct.
		// The AWS documentation says that the date should be within 15 minutes of now, so pass 900 (15 * 60) to
		// the verifyValidDate() function.
		return ($this->verifyValidDate(900) && $this->verifySignature());
	}
	
	/**
	 * verifyValidDate
	 * Checks that any date supplied in the HTTP header is within the specified range.
	 *
	 * @param int $validRangeInSeconds The range, in seconds, that the date should be confirmed to be within.
	 *
	 * @return	bool
	 */
	protected function verifyValidDate($validRangeInSeconds)
	{
		if (empty($validRangeInSeconds))
		{
			LogError("No valid range specified");
			return false;
		}
	
		$date_for_comparison = $this->getDateForComparison();
		if (empty($date_for_comparison))
		{
			LogWarning('No date header found');
			return false;
		}
	
		// Get the time supplied in the HTTP request as a timestamp.
		$supplied_time = strtotime($date_for_comparison);

		// Determine the earliest and latest valid timestamps, based on 15 minutes from now.
		$current_time = time();
		$earliest_valid_timestamp = $current_time - $validRangeInSeconds;
		$latest_valid_timestamp = $current_time + $validRangeInSeconds;

		// Check if the supplied time is outside the allowable range and return false if not.
		if (($supplied_time < $earliest_valid_timestamp) || ($latest_valid_timestamp < $supplied_time))
		{
			LogWarning('Supplied date is outside the valid range of 15 minutes from now');
			return false;
		}
		else
		{
			return true;
		}
	}

	/**
	 * verifySignature
	 * Generates a signature based on the content of the HTTP request and returns true
	 * if it matches the signature found in the Authorization parameter.  If it doesn't
	 * match, or there is no Authorization parameter, returns false.
	 *
	 * @return	bool
	 */
	protected function verifySignature()
	{
		$authentication_info = $this->getAuthenticationInfo();
		if (empty($authentication_info))
		{
			LogWarning('No authentication information found');
			return false;
		}
		
		$aws_access_key_id_and_signature = $this->extractAwsAccessKeyIdAndSignature($authentication_info);
		if (empty($aws_access_key_id_and_signature) || (sizeof($aws_access_key_id_and_signature) !== 2))
		{
			LogWarning('Authentication information was not populated properly');
			return false;
		}

		$aws_secret_access_key = $this->getAwsSecretAccessKey($aws_access_key_id_and_signature[0]);
		if (empty($aws_secret_access_key))
		{
			LogWarning('AWS Secret Access Key could not be found for the passed AWS Access Key ID: ' . $aws_access_key_id_and_signature[0]);
			return false;
		}
		
		if (empty($aws_access_key_id_and_signature[1]))
		{
			LogWarning('No signature found');
			return false;
		}
		
		$string_to_sign = $this->getStringToSign();
		
		if (empty($string_to_sign))
		{
			LogWarning('Calculated string to sign was null');
			return false;
		}
		
		$created_signature = $this->createSignature($aws_secret_access_key, $string_to_sign);
		if (empty($created_signature))
		{
			LogWarning('Calculated signature was null');
			return false;
		}
		
		if (strcmp($created_signature, $aws_access_key_id_and_signature[1]) !== 0)
		{
			LogWarning("Authentication failed, created signature and passed signature were different:");
			LogWarning("  Passed: $aws_access_key_id_and_signature[1]");
			LogWarning("  Created: $created_signature");
			return false;
		}
		else
		{
			LogDebug("Authenticated");
			return true;
		}
	}
	
	/**
	 * getAuthorization
	 * Gets the value held in the Authorization HTTP parameter, if there is one.
	 * Returns null if the parameter doesn't exist.
	 *
	 * @return	string
	 */
	protected function getAuthorization()
	{
		if (isset($this->headers['Authorization']))
		{
			return $this->headers['Authorization'];
		}

		return null;
	}
	
	/**
	 * getAuthenticationInfo
	 * Gets the AWS Access Key ID and Signature from the Authorization HTTP parameter, if
	 * if exists, removing the preceding 'AWS ' prefix.
	 * Returns null if the Authorization parameter doesn't exist, or if the value held in it
	 * is not prefixed with 'AWS '.
	 *
	 * @return	string
	 */
	protected function getAuthenticationInfo()
	{
		$authorization_parameter = $this->getAuthorization();
		if (empty($authorization_parameter) === false)
		{
			$aws_prefix = 'AWS ';
			if (strpos($authorization_parameter, $aws_prefix, 0) === 0)
			{
				return substr($authorization_parameter, strlen($aws_prefix));
			}
		}

		return null;
	}
	
	/**
	 * extractAwsAccessKeyIdAndSignature
	 * Extracts the AWS Access Key ID and Signature from the passed authentication
	 * info as a two element array.
	 * Returns null if the authentication info is not set, or if the colon delimiter
	 * can't be found.
	 *
	 * @param	string	$authenticationInfo	The AWS Access Key and Signature separated by a colon
	 *
	 * @return	array
	 */
	protected function extractAwsAccessKeyIdAndSignature($authenticationInfo)
	{
		if (empty($authenticationInfo) === false)
		{
			// Don't just explode the string based on ':', since it may be possible for a
			// colon to form part of the signature, in which case we'd split the signature as well.
			// Use strpos to find the first occurrence of the colon.
			$delimiter_pos = strpos($authenticationInfo, ':');
			if ($delimiter_pos > 0)
			{
				$access_key_id = substr($authenticationInfo, 0, $delimiter_pos);
				$signature = substr($authenticationInfo, ($delimiter_pos + 1));
				return array($access_key_id, $signature);
			}
		}
		
		return null;
	}
	
	/**
	 * getSecretKey
	 * Returns the AWS Secret Access Key which corresponds with the passed AWS Access Key ID.
	 * Currently the key is hard coded, in future this might get the key from a configuration file
	 * or database.
	 *
	 * @param	string	$awsAccessKeyId
	 *
	 * @return	string
	 */
	protected function getAwsSecretAccessKey($awsAccessKeyId)
	{
		try
		{
			$auth_data_accessor = new AuthDataConfigAccessor();
			return $auth_data_accessor->getAwsSecretAccessKey($awsAccessKeyId);
		}
		catch (\Exception $e)
		{
			LogError("Failed to get AWS Secret Access Key with AWS Access Key ID: $awsAccessKeyId");
			return null;
		}
	}
	
	/**
	 * createSignature
	 * Creates an AWS signature from the passed AWS secret access key and string to sign.
	 *
	 * @param	string	$awsSecretAccessKey
	 * @param	string	$stringToSign
	 *
	 * @return	string
	 */
	protected function createSignature($awsSecretAccessKey, $stringToSign)
	{
		$base64_encoded = base64_encode($this->createHmacSha1Hash($awsSecretAccessKey, $stringToSign));
		return $base64_encoded;
	}
	
	/**
	 * createHmacSha1Hash
	 * Creates an RFC 2104HMAC-SHA1 has from the passed AWS secret access key and string to sign.
	 *
	 * @param	string	$awsSecretAccessKey
	 * @param	string	$stringToSign
	 *
	 * @return	string
	 */
	protected function createHmacSha1Hash($awsSecretAccessKey, $stringToSign)
	{
		return hash_hmac('sha1', $stringToSign, $awsSecretAccessKey, true);
	}
	
	/**
	 * getStringToSign
	 * Constructs the string to sign from the various parameters found in the HTTP request.
	 *
	 * @return	string
	 */
	protected function getStringToSign()
	{
		return (
				$this->getHttpVerb() . "\n" .
				$this->getContentMd5() . "\n" .
				$this->getContentType() . "\n" .
				$this->getDate() . "\n" .
				$this->getCanonicalizedAmzHeaders() .
				$this->getCanonicalizedResource()
		);
	}
	
	/**
	 * getHttpVerb
	 * Returns the HTTP verb (e.g. GET, POST, etc) from the HTTP request.
	 *
	 * @return	string
	 */
	protected function getHttpVerb()
	{
		return strtoupper($this->method);
	}
	
	/**
	 * getContentMd5
	 * Returns the value of the Content-MD5 HTTP parameter, if it is present.
	 * Returns null if the parameter is not found.
	 *
	 * @return	string
	 */
	protected function getContentMd5()
	{
		return isset($this->headers['Content-MD5']) ? $this->headers['Content-MD5'] : null;
	}
	
	/**
	 * getContentType
	 * Returns the value of the Content-Type HTTP parameter, if it is present.
	 * Returns null if the parameter is not found.
	 *
	 * @return	string
	 */
	protected function getContentType()
	{
		return isset($this->headers['Content-Type']) ? $this->headers['Content-Type'] : null;
	}
	
	/**
	 * getDate
	 * Returns the date value which should be used for creation of the string to sign.
	 * 
	 * Returns null if a valid value can not be found.
	 *
	 * @return	string
	 */
	protected function getDate()
	{
		// The section 'Time Stamp Requirement' in the AMZ documentation says that if the 'x-amz-date' header is present, this
		// should be used instead of the 'Date' header, and the 'Date' header string in the string to sign should be blank.
		// The 'x-amz-date' header will be included in the canonicalized headers, so if we find that this header exists, just
		// return null.
		// Note that the example in the documentation seems to be wrong, the 'Date' section in the string to sign is populated with
		// the value of the 'x-amz-date' header, rather than being blank, with the 'x-amz-date' header featuring later.  The
		// implementation here matches the description in the 'Time Stamp Requirement', as well as the implementation in libs3.
		if (isset($this->headers['x-amz-date']))
		{
			return null;
		}
		else
		{
			return isset($this->headers['Date']) ? $this->headers['Date'] : null;
		}
	}
	
	/**
	 * getDateForComparison
	 * Returns the date value which should be used for comparison to the current time to check that
	 * the request does not have a skewed date value.
	 *
	 * Returns null if a valid value can not be found.
	 *
	 * @return	string
	 */
	protected function getDateForComparison()
	{
		// The section 'Time Stamp Requirement' in the AMZ documentation says that if the 'x-amz-date' header is present, this
		// should be used instead of the 'Date' header, so we check for the presence of this field and, if found, return it, otherwise
		// we return the 'Date' header.  If neither is found, we return null.
		if (isset($this->headers['x-amz-date']))
		{
			return $this->headers['x-amz-date'];
		}
		else if (isset($this->headers['Date']))
		{
			return $this->headers['Date'];
		}
		else
		{
			return null;
		}
	}
	
	
	/**
	 * getCanonicalizedAmzHeaders
	 * Returns the 'canonicalized AMZ headers', combining all 'x-amz-' prefixed parameters according to AWS documentation.
	 * Returns null if there are no headers with this prefix.
	 *
	 * @return	string
	 */
	protected function getCanonicalizedAmzHeaders()
	{
		$amz_meta_prefix = 'x-amz-';
		
		// We need to create a copy of the array so we can sort it without disturbing the originally passed data.
		$headers_array = $this->headers;
		
		// Sort the headers by the array key (the header name), as specified in AWS documentation.
		if (ksort($headers_array) === false)
		{
			LogWarning('A problem occurred when sorting the parameters: ' . print_r($headers_array, true));
		}

		// Loop through the headers, identifying which ones need to be canonicalized, changing these names to
		// lower case and combining any duplicated headers.
		$amz_headers = array();
		foreach ($headers_array as $key => $val)
		{
			// Check if the header is one we need to canonicalize.
			if (strpos($key, $amz_meta_prefix) === 0)
			{
				// Convert the header name to lower case, as specified in AWS documentation.
				$key = strtolower($key);
	
				if (isset($amz_headers[$key]))
				{
					$amz_headers[$key] = $amz_headers[$key] . ',' . $val;
				}
				else
				{
					$amz_headers[$key] = $val;
				}
			}
		}

		// Construct a single string to return.
		$canonicalized_amz_headers = null;
		foreach ($amz_headers as $key => $val)
		{
			$canonicalized_amz_headers .= $key . ':' . $val . "\n";
		}
		
		return $canonicalized_amz_headers;
	}
	
	/**
	 * getCanonicalizedResource
	 * Returns the 'canonicalized resources', combining all HTTP resources according to AWS documentation.
	 *
	 * @return	string
	 */
	protected function getCanonicalizedResource()
	{
		$canonicalized_resource_string = null;

		// Both the path and (if specified) the bucket values held in the URI need to be added to the canonicalized resource string.
		// We get these various elements in the params passed to us, so use these to assemble the string.
		foreach ($this->params as $uri_element)
		{
			$canonicalized_resource_string .= '/' . $uri_element;
		}
		
		// If no bucket or URI was specified, the AWS documentation says we should just append a '/'.
		if (empty($canonicalized_resource_string))
		{
			$canonicalized_resource_string = '/';
		}
		
		// If there are any resources specified, these should be appended.
		// Below is a simple case assuming that at most one resource is specified, the AWS documentation describes how to deal
		// with multiple resources, but we don't expect to receive requests specifying multiple resources at this point, so
		// it simple.
		if ($this->queryString != '')
		{
			// The following few lines prefix the query string with a / if the current string doesn't already end with one.
			// This doesn't appear to be necessary according to the 'Constructing the CanonicalizedResource Element'
			// section of the AWS documentation, but later examples show that the query string is prefixed with a /, and libs3 does
			// it this way, too, so we'll do it here.
			if ($canonicalized_resource_string[(strlen($canonicalized_resource_string) -1)] !== '/')
			{
				$canonicalized_resource_string .= '/';
			}

			$canonicalized_resource_string .= '?' . $this->queryString;
		}
		
		return $canonicalized_resource_string;
	}	
}

}