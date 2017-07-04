# check if a given directory is empty
function(is_directory_empty _dir _var)
    set(RESULT NO)
    file(GLOB FILES_FOUND DIR)
    list(LENGTH FILES_FOUND NUMBER_OF_FILES_FOUND)
    if(NUMBER_OF_FILES_FOUND EQUAL 0)
        set(RESULT YES)
    endif()

    set(${_var} "${RESULT}" PARENT_SCOPE)
endfunction()

# Setup git submodules; make sure SUBMODULE_PATH is created afterwards
function(setup_git_submodules)
    include(CMakeParseArguments)
    cmake_parse_arguments(
            PA_
            ""
            "SUBMODULE_PATH;BASE_PATH"
            ""
            ${ARGN})
    if(NOT DEFINED PA__SUBMODULE_PATH)
        message(FATAL_ERROR "setup_git_submodules: SUBMODULE_PATH must be specified")
        return()
    endif()

    if(NOT DEFINED PA__BASE_PATH)
        set(PA__BASE_PATH ${CMAKE_HOME_DIRECTORY})
        message("setup_git_submodules: BASE_PATH not given, setting BASE_PATH to ${CMAKE_HOME_DIRECTORY}")
    endif()

    is_directory_empty("${PA__BASE_PATH}/${PA__SUBMODULE_PATH}" SUBMODULE_NOT_INIT)
    if(SUBMODULE_NOT_INIT)
        find_package(Git QUIET)
        if(NOT GIT_FOUND)
            message(FATAL_ERROR "setup_git_submodules: Git not found")
        endif()

        execute_process(COMMAND
                "${GIT_EXECUTABLE}" submodule update --init --recursive "${PA__SUBMODULE_PATH}"

                WORKING_DIRECTORY "${PA__BASE_PATH}"
                RESULT_VARIABLE res
                )
        if(NOT res EQUAL 0)
            message(FATAL_ERROR "setup_git_submodules: Git submodule-init failed")
        endif()
    endif()
endfunction()

# Setup all directories within a base-path into CMAKE_MODULE_PATH
macro(setup_cmake_modules _base_path)
    foreach(_submodule ${ARGN})
        if(NOT EXISTS "${_base_path}/${_submodule}")
            message(FATAL_ERROR "setup_cmake_modules: Submodule '${_submodule}', not found")
        endif()
        set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${_base_path}/${_submodule}")
    endforeach()
endmacro()
