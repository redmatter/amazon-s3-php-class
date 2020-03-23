set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)

set(HEADER_FILES
        directory_loader.hpp
        database_sqlite.hpp
        cdr_inspector.hpp
        cdr_dir_monitor.hpp
        archivable/queue.hpp
        archivable/catalogue.hpp
        archivable/utils.hpp
        archivable/error.hpp
        net/archived_client.hpp
        net/detail/retry.hpp
        put_group_cache.hpp
        put_group.hpp
        )

include_directories(
        /usr/include/cpp-netlib
        ${rml-queue_SOURCE_DIR}
        ${rml-utils_SOURCE_DIR}
        ${librml_INC_DIR}
        ../common
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (NOT EXISTS ${CMAKE_SOURCE_DIR}/external/sqlite)
        execute_process(COMMAND make -C ${CMAKE_SOURCE_DIR}/external sqlite
                RESULT_VARIABLE res)
        if (NOT res STREQUAL "0" OR NOT EXISTS ${CMAKE_SOURCE_DIR}/external/sqlite)
                message(FATAL_ERROR "Failed to setup sqlite")
        endif()
endif()
find_package(PkgConfig REQUIRED)
set( ENV{PKG_CONFIG_PATH} "${CMAKE_SOURCE_DIR}/external/sqlite/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
        -DBOOST_CHRONO_VERSION=2
)

add_executable(rmcollectd.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmcollectd.bin
        archiving-common

        pugixml
        cryptopp
        uuid
        ${SQLITE3_LINK_LIBRARIES}

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)
target_include_directories(rmcollectd.bin PUBLIC ${SQLITE3_INCLUDE_DIRS})
target_compile_options(rmcollectd.bin PUBLIC ${SQLITE3_CFLAGS_OTHER})

add_subdirectory(tests)
