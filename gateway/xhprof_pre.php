<?php

if (extension_loaded('xhprof'))
{
	$enabled_calls = array(
		'/',
	);
	
	$enabled_ips = array(
		'0.0.0.0/32',
	);

	require_once 'rmapp/class.IPCheck.php';

	$xhprof_on = false;
	
	$ipchk = new IPCheck($enabled_ips);
	if ($ipchk->is_allowed($_SERVER['REMOTE_ADDR']))
	{
		if (count($enabled_calls))
		{
			$regx = '';
			foreach ($enabled_calls as $call)
				$regx .= (($regx?'|':'').'('.preg_quote($call, '#').')');
		
			$regx = '#^('.$regx.')#';
			if (preg_match($regx, $_SERVER['PATH_INFO']))
			{
				$xhprof_on = true;
					
				unset($enabled_calls);
				unset($regx);
			}
		}

		if (!$xhprof_on)
			if (false !== ($headers = apache_response_headers()))
				$xhprof_on = isset($headers['X-EnableProfiler']) && ('true' == strtolower($headers['X-EnableProfiler']));

		if($xhprof_on)
		{
			xhprof_enable(XHPROF_FLAGS_CPU | XHPROF_FLAGS_MEMORY);

			// 1. elapsed time + memory + CPU profiling; and ignore built-in (internal) functions
			// xhprof_enable(XHPROF_FLAGS_NO_BUILTINS | XHPROF_FLAGS_CPU | XHPROF_FLAGS_MEMORY);

			// 2. elapsed time profiling; ignore call_user_func* during profiling
			/*
			xhprof_enable( 0, array('ignored_functions' =>  array(
							'call_user_func',
							'call_user_func_array'))); 
			*/

			// 3. elapsed time + memory profiling; ignore call_user_func* during profiling
			/*
			xhprof_enable( XHPROF_FLAGS_MEMORY, array(
						'ignored_functions' =>  array(
							'call_user_func',
							'call_user_func_array')));
			*/

			register_shutdown_function(function() use($xhprof_on) {
				global $_SERVER;

				if (!$xhprof_on)
					return;

				$xhprof_data = xhprof_disable();

				$namespc = explode('.', $_SERVER['HTTP_HOST']);
				$namespc = $namespc[0];

				$XHPROF_ROOT = "/var/www/xhprof";
				include_once $XHPROF_ROOT . "/xhprof_lib/utils/xhprof_lib.php";
				include_once $XHPROF_ROOT . "/xhprof_lib/utils/xhprof_runs.php";

				$xhprof_runs = new XHProfRuns_Default();
				$run_id = $xhprof_runs->save_run($xhprof_data, $namespc);
			});
		}
	}
	
	unset($ipchk);
}
