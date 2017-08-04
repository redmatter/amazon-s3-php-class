cmake_minimum_required(VERSION 3.6)

option(BUILD_TESTS "Build tests" ON)
if (NOT BUILD_TESTS)
    return()
endif()

project(collectd_tests)

if (NOT DEFINED googletest_BINARY_DIR)
    # --------------- setup tests ---------------
    download_project(PROJ googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.8.0
            UPDATE_DISCONNECTED 1
            )
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif ()
find_package(Threads)

# Enable CTest; it provides means to invoke all tests from single command `ctest -N`
include(CTest)
enable_testing()

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION
)

include(GTestAddTestsFromDirectory)
gtest_add_tests_from_directory(
        DIRECTORY .
        NAME_PREFIX tests/
        LIBRARIES Threads::Threads ${Boost_LIBRARIES} archiving-common cryptopp
)
