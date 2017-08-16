project(collectd)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)
find_package(SQLite3 REQUIRED)

set(HEADER_FILES
        directory_loader.hpp
        database_sqlite.hpp
        cdr_inspector.hpp
        cdr_dir_monitor.hpp
#        pre.h
        archivable/queue.hpp
        archivable/catalogue.hpp
        archivable/utils.hpp
        archivable/error.hpp
        )

include_directories(
        /usr/include/cpp-netlib
        ${SQLITE3_INCLUDE_DIRS}
        ${rml-queue_SOURCE_DIR}
        ../common
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
)

add_executable(rmcollectd.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmcollectd.bin
        archiving-common

        pugixml
        cryptopp
        uuid
        sqlite3

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)

add_subdirectory(tests)
