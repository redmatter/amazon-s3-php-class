project(archived)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(HEADER_FILES
        call_buffering_recording.hpp
        clear_cache.hpp
        compress.hpp
        converter.hpp
        coreapi_user.hpp
        crypto.hpp
        database_cdrmunch.hpp
        database_internal.hpp
        database_retention.hpp
        database_retention_delete.hpp
        dispatcher.hpp
        fcgi_storage_access.hpp
        globals.h
        monitor.hpp
        operation_data.hpp
        org_policy_versions.hpp
        purge.hpp
        put_server.hpp
        put_session.hpp
        retention_data_flusher.hpp
        retention_purge.cpp
        retention_purge.hpp
        retention_purge_runner.hpp
        retention_record.hpp
        state_action.hpp
        state_reactor.hpp
        storage_backend.hpp
        storage_endpoint.hpp
        storage.hpp
        storage_operation.hpp
        storage_runner.hpp
        suppress_headers_check.hpp
        types.hpp
        whisper_client.hpp
        macros.h
        file_format/headers.hpp
        file_format/policies.hpp
        file_format/detail/parameter.hpp
        config/purge_restrictions.hpp
        legal_hold/cache.hpp
        legal_hold/status.hpp
        net/response.hpp
        net/detail/make_response.hpp
        net/detail/response.hpp
        net/put/session.hpp
        net/put/error.hpp
        net/put/request.hpp
        net/put/exception.hpp)

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)
find_package(MySQL REQUIRED)
find_package(MySQL++ REQUIRED)
find_package(cppnetlib 0.11.1 CONFIG REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
        ${CPPNETLIB_INCLUDE_DIRS}
        ${rml-database-mysql_SOURCE_DIR}
        ${rml-utils_SOURCE_DIR}
        ${librml_INC_DIR}
        /usr/include/threadpool
        ${MYSQL_INCLUDE_DIR}
        ../common
)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

        -DRML_DATABASE_MYSQL_DEBUG_ENABLED

        -DBOOST_CHRONO_VERSION=2
)

add_executable(rmarchived.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmarchived.bin
        archiving-common
        cryptopp

        cppnetlib-uri
        pugixml

        crypto
        s3

        fcgi

        mysqlclient
        mysqlpp

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)
