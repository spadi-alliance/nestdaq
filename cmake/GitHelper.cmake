# This file reads and displays git tag information.
# It also reflects it in the project version.


# Set default values
set(PROJECT_VERSION_MAJOR "0")
set(PROJECT_VERSION_MINOR "0")
set(PROJECT_VERSION_PATCH "0")
set(PROJECT_VERSION_PRERELEASE "")
set(COMMIT_COUNT "0")
set(COMMIT_HASH "0000000")
set(DIRTY_FLAG "")

# Check if Git is available 
find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    message(STATUS "git found")

    # Get git describe output (= a string that reflects the state of the current commit)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --long --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE GIT_DESCRIBE_ERROR
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )
    
    if(GIT_DESCRIBE_RESULT EQUAL 0)
        message(STATUS "git describe output: ${GIT_DESCRIBE}")
        
        # Extract tag name, commit number, commit hash, and prerelease identifier using regular expressions
        # If the tag starts with a "v" (for example, "v1.2.3"), remove the "v"
        # e.g. v1.2.3-alpha.1-4-gabcd1234-dirty
        
        if(GIT_DESCRIBE MATCHES "^v?([0-9]+)\.([0-9]+)\.([0-9]+)(-[a-zA-Z]+[0-9]+)?-([0-9]+)-g([0-9a-f]+)(-dirty)?")
            set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
            set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
            set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
            
            if(CMAKE_MATCH_4)
                set(PROJECT_VERSION_PRERELEASE "${CMAKE_MATCH_4}")
            endif()
            # Extract the number of commits and hash
            set(COMMIT_COUNT "${CMAKE_MATCH_5}")
            set(COMMIT_HASH  "${CMAKE_MATCH_6}")
        else()
            message(WARNING "Git tag format is different than expected: ${GIT_DESCRIBE}")
        endif()
        
        # Check the working tree is clean
        string(FIND "${GIT_DESCRIBE}" "dirty" IS_DIRTY)
        if(IS_DIRTY GREATER -1)
            set(DIRTY_FLAG "dirty")
        endif()
        
    else()
        message(STATUS "git describe failed: ${GIT_DESCRIBE_ERROR}")
        #message(WARNING "No Git tag found, using default version.")

        # Extract the number of commits
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE COMMIT_COUNT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        
        # Extract the current commit hash
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    # Get the current branch name
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_BRANCH STREQUAL "HEAD")
        # Detached HEAD state
        set(GIT_BRANCH "detached")
        set(GIT_REMOTE_URL "N/A")
    else()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} config --get branch.${GIT_BRANCH}.remote
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_REMOTE_REPO
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        execute_process(
            COMMAND ${GIT_EXECUTABLE} remote get-url ${GIT_REMOTE_REPO}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_REMOTE_URL
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif()

    # Get the Git commit date and time
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=iso
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    message(WARNING "Git not found or not a repository, using default version.")
endif()


set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

message(STATUS "Parsed Version Information")
message(STATUS "  Project Version:            ${PROJECT_VERSION}")
message(STATUS "  Major:                      ${PROJECT_VERSION_MAJOR}")
message(STATUS "  Minor:                      ${PROJECT_VERSION_MINOR}")
message(STATUS "  Patch:                      ${PROJECT_VERSION_PATCH}")
message(STATUS "  Pre-release:                ${PROJECT_VERSION_PRERELEASE}")
message(STATUS "  Commit Count:               ${COMMIT_COUNT}")
message(STATUS "  Commit Hash:                ${COMMIT_HASH}")
message(STATUS "  Dirty flag:                 ${DIRTY_FLAG}")
message(STATUS "  Branch Name:                ${GIT_BRANCH}")
message(STATUS "  Remote URL:                 ${GIT_REMOTE_URL}")
message(STATUS "  Last Git Commit Date (ISO): ${GIT_COMMIT_DATE}")