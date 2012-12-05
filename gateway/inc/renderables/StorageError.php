<?php
/**
 * Archiving Gateway
 *
 * PHP version 5
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */
namespace rmarchgateway\renderables
{
	
use fluid\Request;

use fluid\HttpException;

use fluid\Renderable;

/**
 * Storage Error response definition
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class StorageError extends HttpException implements Renderable
{
	const AccessDenied = 'AccessDenied';
	const AccountProblem = 'AccountProblem';
	const AmbiguousGrantByEmailAddress = 'AmbiguousGrantByEmailAddress';
	const BadDigest = 'BadDigest';
	const BucketAlreadyExists = 'BucketAlreadyExists';
	const BucketAlreadyOwnedByYou = 'BucketAlreadyOwnedByYou';
	const BucketNotEmpty = 'BucketNotEmpty';
	const CredentialsNotSupported = 'CredentialsNotSupported';
	const CrossLocationLoggingProhibited = 'CrossLocationLoggingProhibited';
	const EntityTooSmall = 'EntityTooSmall';
	const EntityTooLarge = 'EntityTooLarge';
	const ExpiredToken = 'ExpiredToken';
	const IllegalVersioningConfigurationException = 'IllegalVersioningConfigurationException';
	const IncompleteBody = 'IncompleteBody';
	const IncorrectNumberOfFilesInPostRequest = 'IncorrectNumberOfFilesInPostRequest';
	const InlineDataTooLarge = 'InlineDataTooLarge';
	const InternalError = 'InternalError';
	const InvalidAccessKeyId = 'InvalidAccessKeyId';
	const InvalidAddressingHeader = 'InvalidAddressingHeader';
	const InvalidArgument = 'InvalidArgument';
	const InvalidBucketName = 'InvalidBucketName';
	const InvalidBucketState = 'InvalidBucketState';
	const InvalidDigest = 'InvalidDigest';
	const InvalidLocationConstraint = 'InvalidLocationConstraint';
	const InvalidPart = 'InvalidPart';
	const InvalidPartOrder = 'InvalidPartOrder';
	const InvalidPayer = 'InvalidPayer';
	const InvalidPolicyDocument = 'InvalidPolicyDocument';
	const InvalidRange = 'InvalidRange';
	const InvalidRequest = 'InvalidRequest';
	const InvalidSecurity = 'InvalidSecurity';
	const InvalidSOAPRequest = 'InvalidSOAPRequest';
	const InvalidStorageClass = 'InvalidStorageClass';
	const InvalidTargetBucketForLogging = 'InvalidTargetBucketForLogging';
	const InvalidToken = 'InvalidToken';
	const InvalidURI = 'InvalidURI';
	const KeyTooLong = 'KeyTooLong';
	const MalformedACLError = 'MalformedACLError';
	const MalformedPOSTRequest = 'MalformedPOSTRequest';
	const MalformedXML = 'MalformedXML';
	const MaxMessageLengthExceeded = 'MaxMessageLengthExceeded';
	const MaxPostPreDataLengthExceededError = 'MaxPostPreDataLengthExceededError';
	const MetadataTooLarge = 'MetadataTooLarge';
	const MethodNotAllowed = 'MethodNotAllowed';
	const MissingAttachment = 'MissingAttachment';
	const MissingContentLength = 'MissingContentLength';
	const MissingRequestBodyError = 'MissingRequestBodyError';
	const MissingSecurityElement = 'MissingSecurityElement';
	const MissingSecurityHeader = 'MissingSecurityHeader';
	const NoLoggingStatusForKey = 'NoLoggingStatusForKey';
	const NoSuchBucket = 'NoSuchBucket';
	const NoSuchKey = 'NoSuchKey';
	const NoSuchLifecycleConfiguration = 'NoSuchLifecycleConfiguration';
	const NoSuchUpload = 'NoSuchUpload';
	const NoSuchVersion = 'NoSuchVersion';
	const NotImplemented = 'NotImplemented';
	const NotSignedUp = 'NotSignedUp';
	const NotSuchBucketPolicy = 'NotSuchBucketPolicy';
	const OperationAborted = 'OperationAborted';
	const PermanentRedirect = 'PermanentRedirect';
	const PreconditionFailed = 'PreconditionFailed';
	const Redirect = 'Redirect';
	const RequestIsNotMultiPartContent = 'RequestIsNotMultiPartContent';
	const RequestTimeout = 'RequestTimeout';
	const RequestTimeTooSkewed = 'RequestTimeTooSkewed';
	const RequestTorrentOfBucketError = 'RequestTorrentOfBucketError';
	const SignatureDoesNotMatch = 'SignatureDoesNotMatch';
	const ServiceUnavailable = 'ServiceUnavailable';
	const SlowDown = 'SlowDown';
	const TemporaryRedirect = 'TemporaryRedirect';
	const TokenRefreshRequired = 'TokenRefreshRequired';
	const TooManyBuckets = 'TooManyBuckets';
	const UnexpectedContent = 'UnexpectedContent';
	const UnresolvableGrantByEmailAddress = 'UnresolvableGrantByEmailAddress';
	const UserKeyMustBeSpecified = 'UserKeyMustBeSpecified';
	
	const ObjectLocked = 'ObjectLocked';
	const PutOK = 'PutOK';
	
	protected static $responseDefs = null;
	
	protected $awsCode;
	protected $resource;
	protected $requestId;
	
	/**
	 * Ctor
	 * 
	 * @param string    $code      result code
	 * @param object    $resource  storage item only for now
	 * @param string    $requestId request id
	 * @param array     $headers   headers to send back to client
	 * @param Exception $previous  parent exception to pass to HttpException
	 */
	public function __construct($code, $resource, $requestId, $headers=null, $previous=null)
	{
		$this->awsCode = $code;
		$this->resource = $resource;
		$this->requestId = $requestId;
		$this->headers = $headers;
		
		parent::__construct(self::$responseDefs[$code][0], $code, $previous);
	}
	
	/**
	 * getAwsCode
	 * Accessor for awsCode
	 *
	 * @return string
	 */
	public function getAwsCode()
	{
		return $this->awsCode;
	}
	
	/**
	 * getMessageFromCode
	 * Returns the message based on the passed awsCode.
	 * Returns null if the code is not recognised.
	 *
	 * @param string $awsCode awsCode to be converted to message
	 *
	 * @return string
	 */
	public function getMessageFromCode($awsCode)
	{
		if (isset(self::$responseDefs[$awsCode][1]))
		{
			return self::$responseDefs[$awsCode][1];
		}
		else
		{
			return null;
		}
		
	}
	
	/**
	 * (non-PHPdoc)
	 * @see \fluid\Renderable::render()
	 */
	public function render()
	{
		$body = new \DOMMaker('Error');
		$body->nodeCreate('Code', $this->awsCode);
		$body->nodeCreate('Message', self::$responseDefs[$this->awsCode][1]);
		$body->nodeCreate('Resource', is_object($this->resource)? $this->resource->id: $this->resource);
		$body->nodeCreate('RequestId', $this->requestId);
		$body = $body->getDOMDocument()->saveXML();
		
		header('Expires: Mon, 01 Dec 1999 01:00:00 GMT');
		header('Cache-Control: no-cache, must-revalidate');
		header('Pragma: no-cache');
		header('HTTP/1.0 '.$this->getCode().' '.$this->getMessage());
		if (isset($this->headers) && is_array($this->headers))
			foreach ($this->headers as $header)
				call_user_func_array('header', is_array($header)? $header: array($header));
		
		echo $body;
	}

	/**
	 * Make internal error response for Exceptions
	 * 
	 * @param Exception $e 'from' exception 
	 * 
	 * @return StorageError
	 */
	public static function FromException(\Exception $e)
	{
		return new self(self::InternalError, 'unknown', Request::Get('id'), $e);
	}
	
	/**
	 * Static initialiser
	 * 
	 * @return none
	 */
	public static function Init()
	{
		if (is_null(self::$responseDefs))
		{
			self::$responseDefs = array(
				self::AccessDenied => array(403, 'Access Denied'),
				self::AccountProblem => array(403, 'There is a problem with your AWS account that prevents the operation from completing successfully. Please use Contact Us.'),
				self::AmbiguousGrantByEmailAddress => array(400, 'The e-mail address you provided is associated with more than one account.'),
				self::BadDigest => array(400, 'The Content-MD5 you specified did not match what we received.'),
				self::BucketAlreadyExists => array(409, 'The requested bucket name is not available. The bucket namespace is shared by all users of the system. Please select a different name and try again.'),
				self::BucketAlreadyOwnedByYou => array(409, 'Your previous request to create the named bucket succeeded and you already own it.'),
				self::BucketNotEmpty => array(409, 'The bucket you tried to delete is not empty.'),
				self::CredentialsNotSupported => array(400, 'This request does not support credentials.'),
				self::CrossLocationLoggingProhibited => array(403, 'Cross location logging not allowed. Buckets in one geographic location cannot log information to a bucket in another location.'),
				self::EntityTooSmall => array(400, 'Your proposed upload is smaller than the minimum allowed object size.'),
				self::EntityTooLarge => array(400, 'Your proposed upload exceeds the maximum allowed object size.'),
				self::ExpiredToken => array(400, 'The provided token has expired.'),
				self::IllegalVersioningConfigurationException => array(400, 'Indicates that the Versioning configuration specified in the request is invalid.'),
				self::IncompleteBody => array(400, 'You did not provide the number of bytes specified by the Content-Length HTTP header'),
				self::IncorrectNumberOfFilesInPostRequest => array(400, 'POST requires exactly one file upload per request.'),
				self::InlineDataTooLarge => array(400, 'Inline data exceeds the maximum allowed size.'),
				self::InternalError => array(500, 'We encountered an internal error. Please try again.'),
				self::InvalidAccessKeyId => array(403, 'The AWS Access Key Id you provided does not exist in our records.'),
				self::InvalidAddressingHeader => array(400, 'You must specify the Anonymous role.'),
				self::InvalidArgument => array(400, 'Invalid Argument'),
				self::InvalidBucketName => array(400, 'The specified bucket is not valid.'),
				self::InvalidBucketState => array(409, 'The request is not valid with the current state of the bucket.'),
				self::InvalidDigest => array(400, 'The Content-MD5 you specified was an invalid.'),
				self::InvalidLocationConstraint => array(400, 'The specified location constraint is not valid. For more information about Regions, see How to Select a Region for Your Buckets.'),
				self::InvalidPart => array(400, 'One or more of the specified parts could not be found. The part might not have been uploaded, or the specified entity tag might not have matched the part\'s entity tag.'),
				self::InvalidPartOrder => array(400, 'The list of parts was not in ascending order.Parts list must specified in order by part number.'),
				self::InvalidPayer => array(403, 'All access to this object has been disabled.'),
				self::InvalidPolicyDocument => array(400, 'The content of the form does not meet the conditions specified in the policy document.'),
				self::InvalidRange => array(416, 'The requested range cannot be satisfied.'),
				self::InvalidRequest => array(400, 'SOAP requests must be made over an HTTPS connection.'),
				self::InvalidSecurity => array(403, 'The provided security credentials are not valid.'),
				self::InvalidSOAPRequest => array(400, 'The SOAP request body is invalid.'),
				self::InvalidStorageClass => array(400, 'The storage class you specified is not valid.'),
				self::InvalidTargetBucketForLogging => array(400, 'The target bucket for logging does not exist, is not owned by you, or does not have the appropriate grants for the log-delivery group.'),
				self::InvalidToken => array(400, 'The provided token is malformed or otherwise invalid.'),
				self::InvalidURI => array(400, 'Couldn\'t parse the specified URI.'),
				self::KeyTooLong => array(400, 'Your key is too long.'),
				self::MalformedACLError => array(400, 'The XML you provided was not well-formed or did not validate against our published schema.'),
				self::MalformedPOSTRequest => array(400, 'The body of your POST request is not well-formed multipart/form-data.'),
				self::MalformedXML => array(400, 'The XML you provided was not well-formed or did not validate against our published schema.'),
				self::MaxMessageLengthExceeded => array(400, 'Your request was too big.'),
				self::MaxPostPreDataLengthExceededError => array(400, 'Your POST request fields preceding the upload file were too large.'),
				self::MetadataTooLarge => array(400, 'Your metadata headers exceed the maximum allowed metadata size.'),
				self::MethodNotAllowed => array(405, 'The specified method is not allowed against this resource.'),
				self::MissingAttachment => array(400, 'A SOAP attachment was expected, but none were found.'),
				self::MissingContentLength => array(411, 'You must provide the Content-Length HTTP header.'),
				self::MissingRequestBodyError => array(400, 'Request body is empty.'),
				self::MissingSecurityElement => array(400, 'The SOAP 1.1 request is missing a security element.'),
				self::MissingSecurityHeader => array(400, 'Your request was missing a required header.'),
				self::NoLoggingStatusForKey => array(400, 'There is no such thing as a logging status sub-resource for a key.'),
				self::NoSuchBucket => array(404, 'The specified bucket does not exist.'),
				self::NoSuchKey => array(404, 'The specified key does not exist.'),
				self::NoSuchLifecycleConfiguration => array(404, 'The lifecycle configuration does not exist.'),
				self::NoSuchUpload => array(404, 'The specified multipart upload does not exist. The upload ID might be invalid, or the multipart upload might have been aborted or completed.'),
				self::NoSuchVersion => array(404, 'Indicates that the version ID specified in the request does not match an existing version.'),
				self::NotImplemented => array(501, 'A header you provided implies functionality that is not implemented.'),
				self::NotSignedUp => array(403, 'Your account is not signed up for the Amazon S3 service. You must sign up before you can use Amazon S3. You can sign up at the following URL: http://aws.amazon.com/s3'),
				self::NotSuchBucketPolicy => array(404, 'The specified bucket does not have a bucket policy.'),
				self::OperationAborted => array(409, 'A conflicting conditional operation is currently in progress against this resource. Please try again.'),
				self::PermanentRedirect => array(301, 'The bucket you are attempting to access must be addressed using the specified endpoint. Please send all future requests to this endpoint.'),
				self::PreconditionFailed => array(412, 'At least one of the preconditions you specified did not hold.'),
				self::Redirect => array(307, 'Temporary redirect.'),
				self::RequestIsNotMultiPartContent => array(400, 'Bucket POST must be of the enclosure-type multipart/form-data.'),
				self::RequestTimeout => array(400, 'Your socket connection to the server was not read from or written to within the timeout period.'),
				self::RequestTimeTooSkewed => array(403, 'The difference between the request time and the server\'s time is too large.'),
				self::RequestTorrentOfBucketError => array(400, 'Requesting the torrent file of a bucket is not permitted.'),
				self::SignatureDoesNotMatch => array(403, 'The request signature we calculated does not match the signature you provided. Check your AWS Secret Access Key and signing method. For more information, see REST Authentication and SOAP Authentication for details.'),
				self::ServiceUnavailable => array(503, 'Please reduce your request rate.'),
				self::SlowDown => array(503, 'Please reduce your request rate.'),
				self::TemporaryRedirect => array(307, 'You are being redirected to the bucket while DNS updates.'),
				self::TokenRefreshRequired => array(400, 'The provided token must be refreshed.'),
				self::TooManyBuckets => array(400, 'You have attempted to create more buckets than allowed.'),
				self::UnexpectedContent => array(400, 'This request does not support content.'),
				self::UnresolvableGrantByEmailAddress => array(400, 'The e-mail address you provided does not match any account on record.'),
				self::UserKeyMustBeSpecified => array(400, 'The bucket POST must contain the specified field name. If it is specified, please check the order of the fields.'),

				self::ObjectLocked => array(423, 'Object already locked or failed to acquire an appropriate lock'),
				self::PutOK => array(200, 'PUT successful')
			);
		}
	}
}

StorageError::Init();
	
}