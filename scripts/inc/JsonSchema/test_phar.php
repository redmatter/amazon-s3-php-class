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

require_once "artifacts/JsonSchema.phar";

$refResolver = new JsonSchema\RefResolver(new JsonSchema\Uri\UriRetriever(), new JsonSchema\Uri\UriResolver());
$schema = $refResolver->resolve('file://' . realpath('schema.json'));

$data = json_decode(file_get_contents('data.json'));

// Validate
$validator = new JsonSchema\Validator();
$validator->check($data, $schema);

if ($validator->isValid())
{
    echo "The supplied JSON validates against the schema.\n";
}
else
{
    echo "JSON does not validate. Violations:\n";
    foreach ($validator->getErrors() as $error)
    {
        echo sprintf("[%s] %s\n", $error['property'], $error['message']);
    }
}
