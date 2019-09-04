set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options system REQUIRED)

set(HEADER_FILES send.hpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(send-data main.cpp ${HEADER_FILES})

target_link_libraries(send-data
        rt
        Threads::Threads
        ${Boost_LIBRARIES}
        )
