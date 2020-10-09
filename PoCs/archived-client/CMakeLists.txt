set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options system REQUIRED)

set(HEADER_FILES ../blow_bubbles.hpp)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
        /usr/include/cpp-netlib
        ${rml-utils_SOURCE_DIR}
        ${librml_INC_DIR}
        ../../common
)

add_executable(poc-archived-client main.cpp ${HEADER_FILES})

target_link_libraries(poc-archived-client
        rt
        Threads::Threads
        ${Boost_LIBRARIES}
        )
