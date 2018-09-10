project(archiving-common)

include(SetupVersion)
setup_version_c()

set(HEADER_FILES
        archivable.hpp
        archived_client.hpp
        basic_database.hpp
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
        hostname.hpp)

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
        /usr/include/cpp-netlib
        /usr/include/threadpool
        ${rml-database-mysql_SOURCE_DIR}
        ${MYSQL_INCLUDE_DIR})

#find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

add_library(archiving-common STATIC ${SOURCE_FILES} ${HEADER_FILES})
