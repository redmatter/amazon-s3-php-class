project(collect-smsd)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(HEADER_FILES
        sms_dir_monitor.hpp
        sms_inspector.hpp
        directory_loader.hpp
#        pre.h
        )

include_directories(
        /usr/include/cpp-netlib
        ../common
)

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)

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

add_executable(rmcollect-smsd.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmcollect-smsd.bin
        archiving-common

        pugixml
        cryptopp
        uuid

        mysqlclient_r
        mysqlpp

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)