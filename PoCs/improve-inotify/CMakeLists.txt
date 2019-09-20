set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS thread program_options system REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(inotify main.cpp)

include_directories(
        ${librml_INC_DIR}
)

target_link_libraries(inotify
        rt
        Threads::Threads
        ${Boost_LIBRARIES}
        )
