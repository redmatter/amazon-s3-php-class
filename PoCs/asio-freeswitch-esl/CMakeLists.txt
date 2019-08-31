project(asio-freeswitch-esl)

set(HEADER_FILES
        esl.h
        fs_event_monitor.hpp)

#include_directories()

find_package(Boost 1.54 COMPONENTS thread system date_time iostreams REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION
)

add_executable(asio-freeswitch-esl main.cpp ${HEADER_FILES})
target_link_libraries(asio-freeswitch-esl
        esl

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)
