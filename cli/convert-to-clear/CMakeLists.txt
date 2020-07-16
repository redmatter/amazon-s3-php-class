project(cli-convert-to-clear)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)
find_package(cppnetlib 0.11.1 CONFIG REQUIRED)

include_directories(
        ${CPPNETLIB_INCLUDE_DIRS}
#        ${rml-utils_SOURCE_DIR}
        ${librml_INC_DIR}
        ${Boost_INCLUDE_DIRS}
        ../../cpp-common
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

add_executable(archiving-cli-convert-to-clear main.cpp)

target_link_libraries(archiving-cli-convert-to-clear
        archiving-common

        cryptopp
        pugixml

        rt
        Threads::Threads
        ${CPPNETLIB_LIBRARIES}
        ${Boost_LIBRARIES}
        )
