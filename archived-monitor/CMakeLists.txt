project(archived-monitor)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(HEADER_FILES
        )

find_package(Boost 1.54 COMPONENTS program_options system REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
        ${librml_INC_DIR}
        ../common
)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1

        -DBOOST_CHRONO_VERSION=2
)

add_executable(archived-monitor main.cpp ${HEADER_FILES})

target_include_directories(archived-monitor SYSTEM PRIVATE ../external/libs3/include)
target_link_libraries(archived-monitor
        archiving-common

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
        ${CPPNETLIB_LIBRARIES}
        )
