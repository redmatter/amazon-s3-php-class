include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

# bail on first error
add_definitions(
        -Wfatal-errors
)

# include_directories()

gtest_add_tests_from_directory(
        DIRECTORY ${PROJECT_SOURCE_DIR}/estuary
        NAME_PREFIX estuary/
        LIBRARIES
            ${Boost_LIBRARIES}
        )
