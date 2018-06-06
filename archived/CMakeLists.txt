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
        )

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)
find_package(MySQL REQUIRED)
find_package(MySQL++ REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
        /usr/include/cpp-netlib
        ${rml-database-mysql_SOURCE_DIR}
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
