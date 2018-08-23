//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 05/06/2018.
//

#ifndef ARCHIVING_ARCHIVED_MACROS_H
#define ARCHIVING_ARCHIVED_MACROS_H

#include <rml/database/mysql/macro/catch_exception.h>
#include <rml/database/mysql/macro/query_timer.h>

#ifdef RETENTION_PURGE_SQL_DEBUG
#define RETENTION_PURGE_SQL_DEBUG_BOOL true
#else
#define RETENTION_PURGE_SQL_DEBUG_BOOL false
#endif

// Add PURGE context for SQL debug logging
#define MYSQL_DEBUG_SQL_PURGE(conn, line) \
    MYSQL_DEBUG_SQL_DEFAULT_IF(RETENTION_PURGE_SQL_DEBUG_BOOL, conn, line)

#endif //ARCHIVING_ARCHIVED_MACROS_H
