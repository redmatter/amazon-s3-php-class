project(archiving-common)

if (NOT (VERSION OR VCS_EPOCH_TIME OR VCS_COMMIT_ID))
    find_package(Git)
endif ()

if (NOT VERSION)
    set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake ${CMAKE_MODULE_PATH})

    include(GetGitRevisionDescription)
    git_describe(VERSION --tags --dirty=-dirty)
endif ()

# VCS epoch time (the timestamp on the last commit)
if (NOT VCS_EPOCH_TIME)
    execute_process(COMMAND
            "${GIT_EXECUTABLE}" log --max-count=1 --format=%ct

            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE res
            OUTPUT_VARIABLE VCS_EPOCH_TIME

            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT res EQUAL 0)
        # if not a git repo (possible if working from a tag-ball), get the current date
        execute_process(COMMAND
                date +%s

                OUTPUT_VARIABLE VCS_EPOCH_TIME

                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif ()
endif ()

function(format_vcs_epoch _var)
    execute_process(COMMAND
            date -d@${VCS_EPOCH_TIME} ${ARGN}

            RESULT_VARIABLE res
            OUTPUT_VARIABLE out

            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT res EQUAL 0)
        message(WARNING "Unable to derive from VCS_EPOCH_TIME:${VCS_EPOCH_TIME}")
        execute_process(COMMAND
                date -d0 ${ARGN}

                RESULT_VARIABLE res
                OUTPUT_VARIABLE out

                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif ()

    message("Set ${_var}: ${out}")

    set(${_var} "${out}" PARENT_SCOPE)
endfunction()

# Works only for GLIBC based systems; breaks for other libc implementations like musl, uClibc, etc
function(glibc_version _var _default)
    execute_process(COMMAND
            getconf GNU_LIBC_VERSION

            RESULT_VARIABLE res
            OUTPUT_VARIABLE out

            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if ((NOT res EQUAL 0) OR out STREQUAL "")
        message(WARNING "getconf: Unknown conf: GNU_LIBC_VERSION")
        set(out "${_default}")
    endif ()

    set(${_var} "${out}" PARENT_SCOPE)
endfunction()

if (NOT VCS_COMMIT_ID)
    execute_process(COMMAND
            "${GIT_EXECUTABLE}" rev-parse --short --verify HEAD

            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE res
            OUTPUT_VARIABLE VCS_COMMIT_ID

            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT res EQUAL 0)
        message(WARNING "Unable to derive VCS_COMMIT_ID")
        set(VCS_COMMIT_ID "deadbee")
    endif ()
endif ()

format_vcs_epoch(VCS_EPOCH_TIMESTAMP +%c)
format_vcs_epoch(VCS_EPOCH_YEAR +%Y)

string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" BUILD_COMPILER)
set(BUILD_COMPILER "${BUILD_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")

glibc_version(BUILD_LIBC "unknown-libc")

# The release number in the case of RPMs; unlike SVN based packages, this can be a string
if (NOT RELEASE_ID)
    set(RELEASE_ID "<unknown-release-id>")
endif ()

# The build hostname
if (NOT BUILD_HOSTNAME)
    cmake_host_system_information(RESULT BUILD_HOSTNAME QUERY HOSTNAME)
endif ()

# The build identifier
if (NOT BUILD_ID)
    set(BUILD_ID "<unknown-build-id>")
endif ()

#parse the version information into pieces.
string(REGEX REPLACE "^v?([0-9]+)\\..*" "\\1" VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v?[0-9]+\\.([0-9]+).*" "\\1" VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v?[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VERSION_PATCH "${VERSION}")
set(VERSION_SHORT "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")

set(COMMON_VERSION_C "${CMAKE_CURRENT_BINARY_DIR}/version.c")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/version.c.in
        ${COMMON_VERSION_C})

set(HEADER_FILES
        archivable.hpp
        archived_client.hpp
        basic_database.hpp
        cache.hpp
        coreapi_eventlog.hpp
        database.hpp
        defines.h
        directory_loader.h
        dir_monitor.hpp
        exception.hpp
        globals.h
        inotify_monitor.hpp
        logstack.h
        md5.hpp
        policy.hpp
        pre.h
        types.hpp
        )

set(SOURCE_FILES
        archived_client.cpp
        defines.cpp
        dir_monitor.cpp
        types.cpp
        ${COMMON_VERSION_C}
        )

add_definitions(
        -D__librml__tmp_fstream__constructor_private__
        -DRML_LOGGER_SYSLOG_ONLY
        -DLOG_SOURCE_LOCATION

        -DARCHIVED_LISTEN_PORT="10100"
        -DDATABASE_SQLITE

        -DCRYPTOPP_ENABLE_NAMESPACE_WEAK=1
)

include_directories(
        /usr/include/cpp-netlib
        /usr/include/threadpool
        ${MYSQL_INCLUDE_DIR}
)

#find_package(Boost 1.54 COMPONENTS program_options thread system filesystem date_time iostreams REQUIRED)

add_library(archiving-common STATIC ${SOURCE_FILES} ${HEADER_FILES})
