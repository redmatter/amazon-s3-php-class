include_directories(${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

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
        /usr/include/cpp-netlib
        ../../common
)

file(GLOB test_cpps ${PROJECT_SOURCE_DIR}/*.cpp)
foreach(test_cpp ${test_cpps})
    get_filename_component(test_bin ${test_cpp} NAME_WE)

    add_executable(${test_bin} ${test_cpp})

    target_link_libraries(${test_bin}
            gtest gtest_main

            archiving-common
            cryptopp

            rt
            Threads::Threads
            ${Boost_LIBRARIES}
            )
endforeach(test_cpp)