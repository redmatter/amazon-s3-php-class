project(archiving-common)

include(SetupVersion)
setup_version_c()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../external/cpp-netlib)
    execute_process(COMMAND make -C ${CMAKE_CURRENT_SOURCE_DIR}/../external cpp-netlib CPP_NETLIB_BUILD_TESTS=OFF CPP_NETLIB_BUILD_EXAMPLES=OFF
            RESULT_VARIABLE res)
    if (NOT res STREQUAL "0" OR NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../external/cpp-netlib)
        message(FATAL_ERROR "Failed to setup cpp-netlib")
    endif()
endif()

list(APPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../external/cpp-netlib)
find_package(cppnetlib 0.11.1 CONFIG REQUIRED)

set(LIBS3_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/../external/libs3/lib/libs3.so.4.1)
add_custom_target(build_libs3 ALL
        COMMAND ${CMAKE_MAKE_PROGRAM} libs3
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../external/
        COMMENT "libs3 makefile target")
add_library(s3 SHARED IMPORTED)
set_property(TARGET s3 APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(s3 PROPERTIES IMPORTED_LOCATION_NOCONFIG "${LIBS3_LIBRARY}")
add_dependencies(s3 build_libs3)

set(HEADER_FILES
        archivable.hpp
        archived_client.hpp
        cache.hpp
        coreapi_eventlog.hpp
        database.hpp
        defines.h
        directory_loader.h
        dir_monitor.hpp
        exception.hpp
        globals.h
        inotify_monitor.hpp
        logstack.h
        md5.hpp
        policy.hpp
        pre.h
        types.hpp
        collectd/callbacks.hpp
        host_ip_change_detector.hpp
        net/archived/error.hpp
        net/archived/connection.hpp
        net/archived/connection_pool.hpp
        net/archived/send_nop.hpp
        net/archived/send_file.hpp
        net/archived/connection_manager.hpp
        net/archived/detail/put_file_session.hpp
        net/archived/detail/put_state.hpp
        net/ip_addr_utils.hpp
        net/archived/detail/ip_pool_map.hpp
        archivable/group.hpp
        hostname.hpp
        archived/stats/averaged_item.hpp
        archived/stats/item.hpp
        archived/stats/trend.hpp
        ../external/runstats/regression.hpp
        ../external/runstats/stats.hpp
        net/archived/response/code.hpp
        net/archived/response/parsed.hpp
        utils/time.hpp
        utils/tee_stream.hpp
        stream_debug.hpp)

set(SOURCE_FILES
        archived_client.cpp
        defines.cpp
        dir_monitor.cpp
        types.cpp
        ${VERSION_C_PATH})

add_definitions(
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"
        -DDATABASE_SQLITE

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1)

include_directories(
        ${CPPNETLIB_INCLUDE_DIRS}
        /usr/include/threadpool
        ${rml-database-mysql_SOURCE_DIR}
        ${librml_INC_DIR}
        ${MYSQL_INCLUDE_DIR})

#find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

add_library(archiving-common STATIC ${SOURCE_FILES} ${HEADER_FILES})
target_include_directories(archiving-common SYSTEM PUBLIC ../external/libs3/include)
