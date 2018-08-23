cmake_minimum_required(VERSION 3.1)

option(USE_CLANG "Build application with clang" OFF)
if (USE_CLANG)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_C_COMPILER clang)
endif (USE_CLANG)

set(CMAKE_CXX_STANDARD 98)

project(archiving)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wunnamed-type-template-args
        HAVE_NO_UNNAMED_TYPE_TEMPLATE_ARGS)

# setup additional cmake module path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake-modules)

# setup git-submodule
include(SetupGitSubmodules)

# make sure DownloadProject is checked-out and added to cmake module path
setup_git_submodules(SUBMODULE_PATH cmake-modules/external/DownloadProject)
setup_cmake_modules(${CMAKE_SOURCE_DIR}/cmake-modules/external DownloadProject)

# initialise DownloadProject
include(DownloadProject)

if (CMAKE_VERSION VERSION_LESS 3.2)
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "")
else ()
    set(UPDATE_DISCONNECTED_IF_AVAILABLE "UPDATE_DISCONNECTED 1")
endif ()

option(BUILD_TESTS "Build tests" ON)
if (BUILD_TESTS)
    download_project(PROJ googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.8.0
            UPDATE_DISCONNECTED 1
            )
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif()

# Use DownloadProject to load cmake-common
download_project(PROJ cmake-common
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/cmake.git
        GIT_TAG develop
        ${UPDATE_DISCONNECTED_IF_AVAILABLE})

# Add cmake-common to CMAKE_MODULE_PATH
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${cmake-common_SOURCE_DIR}")

# Add rml-queue
download_project(PROJ rml-queue
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-queue.git
        GIT_TAG master
        UPDATE_DISCONNECTED 1
        )
add_subdirectory(${rml-queue_SOURCE_DIR})

download_project(PROJ rml-utils
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-utils.git
        GIT_TAG 1.4.1
        UPDATE_DISCONNECTED 1
        )
add_subdirectory(${rml-utils_SOURCE_DIR})

download_project(PROJ rml-database-mysql
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-database-mysql.git
        GIT_TAG 2.0.3
        UPDATE_DISCONNECTED 1
        )
add_subdirectory(${rml-database-mysql_SOURCE_DIR})

download_project(PROJ rml-statistics
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-statistics.git
        GIT_TAG master
        UPDATE_DISCONNECTED 1
        )
add_subdirectory(${rml-statistics_SOURCE_DIR})

add_subdirectory(common)
add_subdirectory(archived)
#add_subdirectory(archived-monitor)
#add_subdirectory(cli)
add_subdirectory(collectd)
add_subdirectory(collect-smsd)
#add_subdirectory(convert)
#add_subdirectory(eml2a)
#add_subdirectory(simbes)
add_subdirectory(estuary)

if (BUILD_TESTS)
    # --------------- setup tests ---------------
    include(GTestAddTestsFromDirectory)

    # Enable CTest; it provides means to invoke all tests from single command `ctest -N`
    include(CTest)
    enable_testing()

    add_subdirectory(tests)
endif ()

option(BUILD_POCS "Build proof-of-concepts" OFF)
if (BUILD_POCS)
    add_subdirectory(PoCs)
endif()
