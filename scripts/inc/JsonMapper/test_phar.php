<?php
/**
 * Test script
 *
 * PHP version 5
 *
 * @category  Test
 * @package   Test
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2016 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @version   SVN: <svn_id>
 * @link      http://www.redmatter.com
 */

require_once "artifacts/JsonMapper.phar";

/**
 * Contact
 *
 * Contact entity
 *
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Contact
{
    public $name;

    /**
     * Address ...
     *
     * @var Address
     */
    public $address;
}

/**
 * Class Address
 *
 * Address entity
 *
 * @category  Library
 * @package   Library
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2014 Red Matter Ltd (UK)
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class Address
{
    public $street;
    public $city;
}

$jsonContact = <<<JSON
{
    "name":"Sheldon Cooper",
    "address":
    {
        "street": "2311 N. Los Robles Avenue",
        "city": "Pasadena"
    }
}
JSON;

$mapper = new JsonMapper();
$contactObject = $mapper->map(json_decode($jsonContact), new Contact());
echo json_encode($contactObject)."\n";
exit(0);
