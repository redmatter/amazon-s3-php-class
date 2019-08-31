set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98")

project(estuary)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

if (NOT EXISTS ../external/cpp-netlib)
        execute_process(COMMAND make -C ../external cpp-netlib CPP_NETLIB_BUILD_TESTS=OFF CPP_NETLIB_BUILD_EXAMPLES=OFF
                RESULT_VARIABLE res)
        if (NOT res STREQUAL "0" OR NOT EXISTS ../external/cpp-netlib)
                message(FATAL_ERROR "Failed to setup cpp-netlib")
        endif()
endif()

list(APPEND CMAKE_PREFIX_PATH ../external/cpp-netlib)

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)
find_package(MySQL REQUIRED)
find_package(MySQL++ REQUIRED)
find_package(cppnetlib 0.11.1 CONFIG REQUIRED)

set(HEADER_FILES
        defines.h
        app/config/common.hpp
        app/config/command_line.hpp
        app/archived_shim.hpp
        app/utils.hpp
        app/error.hpp
        app/queue/queue.hpp
        app/queue/error.hpp
        app/archivable_queue.hpp
        app/put_object_request.hpp
        app/http/server.hpp
        app/http/request_correlation.hpp
        app/http/request_wrapped.hpp
        app/http/detail/path_info.hpp
        app/http/detail/query_map.hpp
        app/http/pending_request_map.hpp
        app/http/pending_request.hpp
        app/http/lexical_cast.hpp app/http/request_progress.hpp)

include_directories(
        ${CPPNETLIB_INCLUDE_DIRS}
        /usr/include/threadpool
        ${MYSQL_INCLUDE_DIR}
        ${rml-utils_SOURCE_DIR}
        ${rml-queue_SOURCE_DIR}
        ${rml-statistics_SOURCE_DIR}
        ${librml_INC_DIR}
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

        -DBOOST_NETWORK_HTTP_SERVER_CONNECTION_BUFFER_SIZE=4096
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
        ${CPPNETLIB_LIBRARIES}
        )

add_subdirectory(tests)
