cmake_minimum_required(VERSION 2.8.4)
project(archiving_tests)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++98")

add_subdirectory(libs/googletest/googletest)
add_subdirectory(common)
