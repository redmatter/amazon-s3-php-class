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
namespace rmarchgateway\policy
{

/**
 * RMBucketPolicyRule
 * Class which encapsulates a RedMatter Bucket Policy rule.
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Andrew Bulford <andrew.bulford@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class RMBucketPolicyRule
{
	const ALLOW_OVERWRITE = 'AllowOverwrite';

	protected $name;
	protected $type;
	protected $value;
	
	/**
	 * Constructor
	 * Constructs the rule from its name, type and value.
	 *
	 * @throws Exception
	 *
	 * @return none
	 */
	public function __construct($name, $type, $value)
	{
		if (isset($name) === false)
		{
			LogError('$name not set');
			throw new \Exception('$name not set');
		}

		if (isset($type) === false)
		{
			LogError('$type not set');
			throw new \Exception('$type not set');
		}

		if (isset($value) === false)
		{
			LogError('$value not set');
			throw new \Exception('$value not set');
		}

		$this->name = $name;
		$this->type = $type;
		
		// Interpret the value based on its type.
		switch ($type)
		{
			case 'bool' :
			{
				$lower_val = strtolower($value);
				if ($lower_val === 'true')
				{
					$this->value = true;
				}
				else if ($lower_val === 'false')
				{
					$this->value = false;
				}
				else
				{
					LogError("Type claimed to be bool but value was not 'true' or 'false'");
					throw new \Exception("Type claimed to be bool but value was not 'true' or 'false'");
				}
				break;
			}
			default :
			{
				$this->value = $value;
				break;
			}
		}
	}

	/**
	 * getName
	 *
	 * @return string
	 */
	public function getName()
	{
		return $this->name;
	}

	/**
	 * getType
	 *
	 * @return string
	 */
	public function getType()
	{
		return $this->type;
	}

	/**
	 * getValue
	 *
	 * @return mixed
	 */
	public function getValue()
	{
		return $this->value;
	}
}

}