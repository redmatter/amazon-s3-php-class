//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 05/06/2018.
//

#ifndef ARCHIVING_ARCHIVED_MACROS_H
#define ARCHIVING_ARCHIVED_MACROS_H

#define SqlDebug(conn, line) \
    LogCoutIf((conn)->is_debug_enabled(), "SQL: " << line)

#define SqlDebugIf(cond, conn, line) \
    LogCoutIf((cond) || (conn)->is_debug_enabled(), "SQL: " << line)

#ifdef RETENTION_PURGE_SQL_DEBUG
#define RETENTION_PURGE_SQL_DEBUG_BOOL true
#else
#define RETENTION_PURGE_SQL_DEBUG_BOOL false
#endif

#define PURGE_SQL_DEBUG(conn, line) \
    SqlDebugIf(RETENTION_PURGE_SQL_DEBUG_BOOL, conn, line)

#endif //ARCHIVING_ARCHIVED_MACROS_H
