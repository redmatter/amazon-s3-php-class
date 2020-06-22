cmake_minimum_required(VERSION 3.14)

option(BUILD_TESTS "Build tests" ON)

option(USE_CLANG "Build application with clang" OFF)
if (USE_CLANG)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_C_COMPILER clang)
endif (USE_CLANG)

# Always build with coverage when build-type is Debug.
if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
set(CMAKE_CXX_FLAGS "--coverage")
endif()

project(archiving)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wunnamed-type-template-args
        HAVE_NO_UNNAMED_TYPE_TEMPLATE_ARGS)

include(FetchContent)

FetchContent_Declare(
        cmake-common
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/cmake.git
        GIT_TAG        master
)

if (BUILD_TESTS)
    FetchContent_Declare(
            googletest
            GIT_REPOSITORY git@github.com:google/googletest.git
            GIT_TAG        release-1.8.1
    )
endif()

FetchContent_Declare(
        librml
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml.git
        GIT_TAG        1.14.0
)

FetchContent_Declare(
        rml-queue
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-queue.git
        GIT_TAG        master
)

FetchContent_Declare(
        rml-utils
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-utils.git
        GIT_TAG        1.4.2
)

FetchContent_Declare(
        rml-database-mysql
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-database-mysql.git
        GIT_TAG        2.0.3
)

FetchContent_Declare(
        rml-statistics
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-statistics.git
        GIT_TAG        1.1.0
)

FetchContent_MakeAvailable(cmake-common)
# Add cmake-common to CMAKE_MODULE_PATH
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${cmake-common_SOURCE_DIR}")

if (BUILD_TESTS)
    FetchContent_MakeAvailable(googletest)
endif()

FetchContent_GetProperties(librml rml-queue rml-utils rml-database-mysql rml-statistics)
if(NOT librml_POPULATED)
    option(BUILD_HEADER_ONLY "" ON) # todo see if we need this
    FetchContent_Populate(librml)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/librml)
    file(CREATE_LINK ${librml_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/librml/rml SYMBOLIC)
    # run build-time templating logic in librml
    add_subdirectory("${CMAKE_CURRENT_BINARY_DIR}/librml/rml")
    set(librml_INC_DIR "${CMAKE_CURRENT_BINARY_DIR}/librml")
endif()

FetchContent_Populate(rml-queue)
FetchContent_Populate(rml-utils)
FetchContent_Populate(rml-database-mysql)
FetchContent_Populate(rml-statistics)

add_subdirectory(common)
add_subdirectory(archived)
add_subdirectory(archived-monitor)
add_subdirectory(cli/convert-to-archive)
add_subdirectory(cli/convert-to-clear)
add_subdirectory(cli/download)
add_subdirectory(collectd)
add_subdirectory(collect-smsd)
add_subdirectory(collect-importd)
add_subdirectory(convert)
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
