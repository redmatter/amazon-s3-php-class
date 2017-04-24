cmake_minimum_required(VERSION 2.8.12)
project(archiving_tests)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")

include(CMakeParseArguments)

macro(gtest_add_tests_from_directory)
    # Find and add tests from the given directory
    # Arguments:
    #  - DIRECTORY Directory to lookup for tests matching test_*.cpp
    #  - NAME_PREFIX Optional name prefix for each test found from the directory
    #  - LIBRARIES Any library required for linking the test

    cmake_parse_arguments(
            PARSED_ARGS
            ""
            "DIRECTORY;NAME_PREFIX"
            "LIBRARIES"
            ${ARGN})
    if (NOT PARSED_ARGS_DIRECTORY)
        message(FATAL_ERROR "DIRECTORY not given")
    endif()

    message("Looking for tests in ${PARSED_ARGS_DIRECTORY}")
    file(GLOB test_cpps ${PARSED_ARGS_DIRECTORY}/test_*.cpp)
    if(NOT test_cpps)
        message(FATAL_ERROR "No tests found in ${PARSED_ARGS_DIRECTORY}")
    endif()
    foreach(test_cpp ${test_cpps})
        get_filename_component(test_bin ${test_cpp} NAME_WE)

        add_executable(${test_bin} ${test_cpp})
        target_link_libraries(${test_bin} gtest gtest_main ${PARSED_ARGS_LIBRARIES})

        message("Adding test ${PARSED_ARGS_NAME_PREFIX}${test_bin}")
        add_test(NAME "${PARSED_ARGS_NAME_PREFIX}${test_bin}"
                COMMAND ${test_bin})
    endforeach(test_cpp)
endmacro(gtest_add_tests_from_directory)

# Add googletest; download and setup from github

configure_file(googletest-download/CMakeLists.txt.in
        googletest-download/CMakeLists.txt)

execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
    message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build .
        RESULT_VARIABLE result
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/googletest-download )
if(result)
    message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Add googletest-download directly to our build. This defines
# the gtest and gtest_main targets.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/googletest-src
        ${CMAKE_CURRENT_BINARY_DIR}/googletest-build )

# The gtest/gtest_main targets carry header search path
# dependencies automatically when using CMake 2.8.11 or
# later. Otherwise we have to add them here ourselves.
if (CMAKE_VERSION VERSION_LESS 2.8.11)
    include_directories("${gtest_SOURCE_DIR}/include")
endif()

# Enable CTest; it provides means to invoke all tests from single command `ctest -N`
include(CTest)
enable_testing()

# Enable individual test-groups
add_subdirectory(common)
