set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98")

project(tests)

include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

# bail on first error
add_definitions(
        -Wfatal-errors
)

# include_directories()

message("CPPNETLIB_INCLUDE_DIRS: ${CPPNETLIB_INCLUDE_DIRS}")

gtest_add_tests_from_directory(
        DIRECTORY .
        NAME_PREFIX tests/
        LIBRARIES
            ${Boost_LIBRARIES}
        )
