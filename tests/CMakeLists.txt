cmake_minimum_required(VERSION 2.8.4)
project(archiving_tests)

if(BUILD_TESTING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")

    # lookup tests from a given directory and set'm up
    macro(gtest_add_directory _directory)
        message("Looking for tests in ${_directory}")
        file(GLOB test_cpps ${_directory}/test_*.cpp)
        foreach(test_cpp ${test_cpps})
            get_filename_component(test_bin ${test_cpp} NAME_WE)

            add_executable(${test_bin} ${test_cpp})
            target_link_libraries(${test_bin} gtest gtest_main ${ARGN})

            message("Adding test ${test_bin}")
            add_test(NAME ${test_bin}
                    COMMAND ${test_bin})
        endforeach(test_cpp)
    endmacro(gtest_add_directory)


    # todo Add google-test ExternalProject

    add_subdirectory(libs/googletest/googletest)

    # Enable CTest; it provides means to invoke all tests from single command `ctest -N`
    include(CTest)
    enable_testing()

    add_subdirectory(common)
endif(BUILD_TESTING)
