//
// Copyright (c) 2017 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 04/12/2017.
//

#ifndef CDRMUNCH_estuary_DEFINES_HPP
#define CDRMUNCH_estuary_DEFINES_HPP

#ifndef SYSLOG_APP_NAME
#define SYSLOG_APP_NAME "rm-estuary"
#endif

#ifndef DEFAULT_APP_NAME
#define DEFAULT_APP_NAME "rmestuary"
#endif

#ifndef PROJECT_NAME
#define PROJECT_NAME "archiving"
#endif

#ifndef DEFAULT_APP_LONG_NAME
#define DEFAULT_APP_LONG_NAME "Archiving Estuary"
#endif

#ifndef CONFIG_FILE_NAME
#define CONFIG_FILE_NAME DEFAULT_APP_NAME".conf"
#endif

#ifndef CONFIG_FILE_PATH
#define CONFIG_FILE_PATH "/etc/"PROJECT_NAME"/"CONFIG_FILE_NAME
#endif

#ifndef PID_FILE_NAME
#define PID_FILE_NAME DEFAULT_APP_NAME"/pid"
#endif

#ifndef PID_FILE_PATH
#define PID_FILE_PATH "/var/run/"PROJECT_NAME"/"PID_FILE_NAME
#endif

#ifndef DEFAULT_LOG_FILE_PATH
#define DEFAULT_LOG_FILE_PATH "syslog"
#endif

#endif //CDRMUNCH_DEFINES_HPP
