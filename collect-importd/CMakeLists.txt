project(collect-importd)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

find_package(MySQL)
find_package(MySQL++)
find_package(cppnetlib 0.11.1 CONFIG REQUIRED)

set(HEADER_FILES
        directory_loader.hpp
        import_dir_monitor.hpp
        inspector.hpp
        archivable/channel.hpp
        archivable/database.hpp
        )

include_directories(
        ${MYSQL_INCLUDE_DIR}
        ${MYSQLPP_INCLUDE_DIR}
        ${CPPNETLIB_INCLUDE_DIRS}
        ${rml-database-mysql_SOURCE_DIR}
        ${librml_INC_DIR}
        ../common
)

find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams regex REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_definitions(
        -Dinline_=
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
)

add_executable(rmcollect-importd.bin main.cpp ${HEADER_FILES})

target_link_libraries(rmcollect-importd.bin
        archiving-common

        pugixml
        cryptopp
        uuid

        ${MYSQL_LIBRARIES}
        ${MYSQLPP_LIBRARIES}
        ${CPPNETLIB_LIBRARIES}

        rt
        Threads::Threads
        ${Boost_LIBRARIES}
)
