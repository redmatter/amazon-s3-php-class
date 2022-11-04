cmake_minimum_required(VERSION 3.17.5)

project(archiving CXX)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
    message(FATAL_ERROR "Compiler ${CMAKE_CXX_COMPILER} has no C++11 support.")
endif()

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
        GIT_TAG        feature/ARCHIVING-125-dockerise-devel
)

FetchContent_Declare(
        rml-queue
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-queue.git
        GIT_TAG        1.2.0
)

FetchContent_Declare(
        rml-utils
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-utils.git
        GIT_TAG        1.5.0
)

FetchContent_Declare(
        rml-database-mysql
        GIT_REPOSITORY ssh://git@bitbucket.redmatter.com/lib/rml-database-mysql.git
        GIT_TAG        2.2.0
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
    file(RELATIVE_PATH librml_SOURCE_DIR_REL ${CMAKE_CURRENT_BINARY_DIR} ${librml_SOURCE_DIR})
    file(CREATE_LINK ../${librml_SOURCE_DIR_REL} ${CMAKE_CURRENT_BINARY_DIR}/librml/rml SYMBOLIC)
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
