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

// require_once 'hPHP/hapi.php';
// $hAPI = new HAPIDBCreds('rmarchgateway', LOG_WARNING);
// define('IN_PRODUCTION', $hAPI->InProduction());

use rmarchgateway\renderables\StorageError;

use fluid\renderables\Response;

define('APP_NAME_', 'rm-archiving-gateway');
define('APP_BASE_NAMESPACE', 'rmarchgateway');
define('APP_CONFIG_FILE', '/etc/archiving/gateway.conf');
define('RM_SYSLOGGER_LOG_MULTILINE', true);
define('RM_SYSLOGGER_NO_INIT_NO_LOGS', false);
define('RM_SYSLOGGER_LOG_SOURCE_CONTEXT', true);

require_once 'rmapp/class.BaseApp.php';

/**
 * Archiving Gateway
 *
 * @category  ArchivingGateway
 * @package   RedMatter_ArchivingGateway
 * @author    Dino Korah <dino.korah@redmatter.com>
 * @copyright 2009-2012 RedMatter
 * @license   http://www.redmatter.com/license private
 * @link      http://www.redmatter.com
 */
class ArchivingGatewayApp extends BaseApp
{
	/**
	 * run()
	 *
	 * @return integer
	 */
	public function run()
	{
		$response = null;

		try
		{
			// get the client
			$client = \fluid\Client::GetInstance();
			
			// setup some routes
			\fluid\Router::AddRoute('storage', 'resources\Storage');
			
			LogDebug($client->request->method);
			LogDebug($client->request->request_uri);

			// route the client
			list($class, $params) = \fluid\Router::GetHandler($client);
			LogDebug(\fluid\Router::$routed_direct, 'routed direct?');
			LogDebug($class, 'routed class');
			LogDebug($params, 'params');

			// instantiate the resource
			$resource = $class::GetResource($client, $params);
			if (\fluid\Router::$routed_direct && !$resource->IsAllowedDirectRouting())
				throw new \fluid\HttpException(404, 'Direct access tto this resource not permitted');

			// Handle request RESTfully
			$response = $resource->HandleRequest();
		}
		catch (\rmarchgateway\renderables\StorageError $e)
		{
			LogDebug($e);
			$response = $e;
		}
		catch (\fluid\HttpException $e)
		{
			LogDebug($e);
			
			if (isset($e->DebugMessage))
				LogDebug($e->DebugMessage, 'HttpException');
			
			$response = Response::FromHttpException($e);
		}
		catch (Exception $e)
		{
			LogWarning($e);
			LogDebug("\n".$e->getTraceAsString());
			$response = StorageError::FromException($e);
		}

		// render the response
		$response->render();
	}
}

$app = ArchivingGatewayApp::getInstance(APP_CONFIG_FILE)->run();

