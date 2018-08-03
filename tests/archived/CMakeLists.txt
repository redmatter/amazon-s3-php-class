include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

# bail on first error
add_definitions(
        -Wfatal-errors
)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION
        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
)

include_directories(
        ../../common
        /usr/include/cpp-netlib/
)

gtest_add_tests_from_directory(
        DIRECTORY ${PROJECT_SOURCE_DIR}/archived
        NAME_PREFIX archived/
        LIBRARIES cryptopp ${Boost_LIBRARIES} archiving-common
)
