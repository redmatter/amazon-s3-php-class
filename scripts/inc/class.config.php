<?php

// NoSniff

/**
 * Archiving Retention
 *
 * PHP version 5
 *
 * @category  config
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

/**
 * config
 *
 * @category  config
 * @package   RedMatter_API
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2010 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class config
{
	/**
	 * Config lookup
	 * 
	 * @param string $key          key
	 * @param mixed  $defaultValue default
	 * 
	 * @return mixed
	 */
	public static function get($key, $defaultValue=null)
	{
		return BaseApp::getInstance()->getConf()->get($key, $defaultValue);
	}
}