set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98")

project(estuary)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)
find_package(MySQL REQUIRED)
find_package(MySQL++ REQUIRED)

set(HEADER_FILES
        defines.h
        app/config/common.hpp
        app/config/command_line.hpp
        app/typedefs.hpp
        app/archived_shim.hpp
        app/archived_client_adaptor.hpp
        app/utils.hpp
        app/error.hpp
        app/queue/queue.hpp
        app/queue/error.hpp
        app/archivable_queue.hpp)

include_directories(
        /usr/include/cpp-netlib
        /usr/include/threadpool
        ${MYSQL_INCLUDE_DIR}
        ${rml-utils_SOURCE_DIR}
        ${rml-queue_SOURCE_DIR}
        ${Boost_INCLUDE_DIRS}
        ../cpp-common
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION
        -DBOOST_CHRONO_VERSION=2

        -DRML_LOG_RUNTIME_LEVEL_CHANGE
        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

        -DARCHIVED_LISTEN_PORT="10100"
)

add_executable(rmestuary.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmestuary.bin
        archiving-common

        cryptopp
        pugixml
        mysqlclient
        mysqlpp
        s3

        rt
        Threads::Threads
        ${Boost_LIBRARIES}

        cppnetlib-uri
        cppnetlib-server-parsers
)

#add_subdirectory(tests)
