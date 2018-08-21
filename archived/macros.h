//
// Copyright (c) 2018 Red Matter Ltd. UK. All rights reserved.
// Unauthorized copying of this file, via any medium is strictly prohibited
// Proprietary and confidential
//
// Created by Dino Korah on 05/06/2018.
//

#ifndef ARCHIVING_ARCHIVED_MACROS_H
#define ARCHIVING_ARCHIVED_MACROS_H

#include <rml/utils/wall_timer.hpp>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>

#define SQL_DEBUG_DEFAULT(conn, line) \
    LogCoutIf((conn)->is_debug_enabled(), "SQL: " << line)

#define SQL_DEBUG_DEFAULT_IF(cond, conn, line) \
    LogCoutIf((cond) || (conn)->is_debug_enabled(), "SQL: " << line)

#ifdef RETENTION_PURGE_SQL_DEBUG
#define RETENTION_PURGE_SQL_DEBUG_BOOL true
#else
#define RETENTION_PURGE_SQL_DEBUG_BOOL false
#endif

#define SQL_DEBUG_PURGE(conn, line) \
    SQL_DEBUG_DEFAULT_IF(RETENTION_PURGE_SQL_DEBUG_BOOL, conn, line)

#define MYSQL_CATCH_LOG_ERROR(e) \
    catch(const mysqlpp::Exception& e) { LogError("MySQL Exception: "<<e.what()); } \
    catch(const std::exception& e) { LogError("Exception: "<<e.what()); } \
    catch(...) { LogError("Unknown Exception"); }

#define MYSQL_CATCH_LOG_WARNING(e) \
    catch(const mysqlpp::Exception& e) { LogWarning("MySQL Exception: "<<e.what()); } \
    catch(const std::exception& e) { LogWarning("Exception: "<<e.what()); } \
    catch(...) { LogWarning("Unknown Exception"); }

#define MYSQL_CHOOSE_DEBUG(mode) SQL_DEBUG_##mode

#define GET_5TH_ARG(A1, A2, A3, A4, A5, ...) A5
#define GET_6TH_ARG(A1, A2, A3, A4, A5, A6, ...) A6

#define MYSQL_QUERY_DEBUG_CONTEXT \
"[thread:" << boost::this_thread::get_id() << ' ' << __FUNCTION__ << ":" BOOST_PP_STRINGIZE(__LINE__) "]"

// ------------------ MYSQL_QUERY_DEBUG_WALL_TIMER_START ------------------

#define MYSQL_QUERY_DEBUG_WALL_TIMER \
    BOOST_PP_CAT(wall_timer_, __LINE__)

#define MYSQL_QUERY_DEBUG_WALL_TIMER_START_IMPL(mode, conn, query) \
    rml::utils::wall_timer MYSQL_QUERY_DEBUG_WALL_TIMER; \
    MYSQL_QUERY_DEBUG(mode, conn, query) \

#define MYSQL_QUERY_DEBUG_WALL_TIMER_START_QP_IMPL(mode, conn, query, qp) \
    rml::utils::wall_timer MYSQL_QUERY_DEBUG_WALL_TIMER; \
    MYSQL_QUERY_DEBUG(mode, conn, query, qp) \

#define MYSQL_QUERY_DEBUG_WALL_TIMER_START_MACRO_CHOOSER(...) \
    GET_5TH_ARG(__VA_ARGS__, MYSQL_QUERY_DEBUG_WALL_TIMER_START_QP_IMPL, MYSQL_QUERY_DEBUG_WALL_TIMER_START_IMPL, )

#ifdef WITHOUT_MYSQL_QUERY_WALL_TIMER

#define MYSQL_QUERY_DEBUG_WALL_TIMER_START(...) \
    MYSQL_QUERY_DEBUG(__VA_ARGS__)

#else

#define MYSQL_QUERY_DEBUG_WALL_TIMER_START(...) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

#endif // ifdef WITHOUT_MYSQL_QUERY_WALL_TIMER

// ------------------ MYSQL_QUERY_DEBUG_WALL_TIMER_STOP ------------------

#ifdef WITHOUT_MYSQL_QUERY_WALL_TIMER

#define MYSQL_QUERY_DEBUG_WALL_TIMER_STOP() (void)0

#else

#define MYSQL_QUERY_DEBUG_WALL_TIMER_STOP() \
    MYSQL_QUERY_DEBUG_WALL_TIMER.stop(); \
    LogMessage(MYSQL_QUERY_DEBUG_CONTEXT " SQL execution took " << MYSQL_QUERY_DEBUG_WALL_TIMER )

#endif // ifdef WITHOUT_MYSQL_QUERY_WALL_TIMER

// ------------------ MYSQL_QUERY_DEBUG ------------------

#define MYSQL_QUERY_DEBUG_IMPL(mode, conn, query) \
    MYSQL_CHOOSE_DEBUG(mode)(conn, MYSQL_QUERY_DEBUG_CONTEXT " " << query.str())

#define MYSQL_QUERY_DEBUG_QP_IMPL(mode, conn, query, qp) \
    MYSQL_CHOOSE_DEBUG(mode)(conn, MYSQL_QUERY_DEBUG_CONTEXT " " << query.str(qp))

#define MYSQL_QUERY_DEBUG_MACRO_CHOOSER(...) \
    GET_5TH_ARG(__VA_ARGS__, MYSQL_QUERY_DEBUG_QP_IMPL, MYSQL_QUERY_DEBUG_IMPL, )

#define MYSQL_QUERY_DEBUG(...) \
    MYSQL_QUERY_DEBUG_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

// ------------------ MYSQL_QUERY_EXECUTE_GET_RESULT ------------------

#define MYSQL_QUERY_EXECUTE_GET_RESULT_IMPL(mode, conn, query, resultVar) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query); \
    const mysqlpp::SimpleResult& resultVar(query.execute()); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_EXECUTE_GET_RESULT_QP_IMPL(mode, conn, query, resultVar, qp) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query, qp); \
    const mysqlpp::SimpleResult& resultVar(query.execute(qp)); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_EXECUTE_GET_RESULT_MACRO_CHOOSER(...) \
    GET_6TH_ARG(__VA_ARGS__, MYSQL_QUERY_EXECUTE_GET_RESULT_QP_IMPL, MYSQL_QUERY_EXECUTE_GET_RESULT_IMPL, )

#define MYSQL_QUERY_EXECUTE_GET_RESULT(...) \
    MYSQL_QUERY_EXECUTE_GET_RESULT_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

// ------------------ MYSQL_QUERY_EXECUTE ------------------

#define MYSQL_QUERY_EXECUTE_IMPL(mode, conn, query) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query); \
    query.execute(); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_EXECUTE_QP_IMPL(mode, conn, query, qp) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query, qp); \
    query.execute(qp); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_EXECUTE_MACRO_CHOOSER(...) \
    GET_5TH_ARG(__VA_ARGS__, MYSQL_QUERY_EXECUTE_QP_IMPL, MYSQL_QUERY_EXECUTE_IMPL, )

#define MYSQL_QUERY_EXECUTE(...) \
    MYSQL_QUERY_EXECUTE_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

// ------------------ MYSQL_QUERY_USE ------------------

#define MYSQL_QUERY_USE_IMPL(mode, conn, query, resultVar) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query); \
    const mysqlpp::UseQueryResult& resultVar(query.use()); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_USE_QP_IMPL(mode, conn, query, resultVar, qp) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query, qp); \
    const mysqlpp::UseQueryResult& resultVar(query.use(qp)); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_USE_MACRO_CHOOSER(...) \
    GET_6TH_ARG(__VA_ARGS__, MYSQL_QUERY_USE_QP_IMPL, MYSQL_QUERY_USE_IMPL, )

#define MYSQL_QUERY_USE(...) \
    MYSQL_QUERY_USE_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

// ------------------ MYSQL_QUERY_STORE ------------------

#define MYSQL_QUERY_STORE_IMPL(mode, conn, query, resultVar) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query); \
    const mysqlpp::StoreQueryResult& resultVar(query.store()); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_STORE_QP_IMPL(mode, conn, query, resultVar, qp) \
    MYSQL_QUERY_DEBUG_WALL_TIMER_START(mode, conn, query, qp); \
    const mysqlpp::StoreQueryResult& resultVar(query.store(qp)); \
    MYSQL_QUERY_DEBUG_WALL_TIMER_STOP()

#define MYSQL_QUERY_STORE_MACRO_CHOOSER(...) \
    GET_6TH_ARG(__VA_ARGS__, MYSQL_QUERY_STORE_QP_IMPL, MYSQL_QUERY_STORE_IMPL, )

#define MYSQL_QUERY_STORE(...) \
    MYSQL_QUERY_STORE_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

#endif //ARCHIVING_ARCHIVED_MACROS_H
