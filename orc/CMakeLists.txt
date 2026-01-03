cmake_minimum_required(VERSION 3.20)
project(decode-orc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Get git commit hash for version
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY
        ERROR_QUIET
    )
    if(NOT GIT_DIRTY EQUAL 0)
        set(GIT_COMMIT_HASH "${GIT_COMMIT_HASH}-dirty")
    endif()
else()
    set(GIT_COMMIT_HASH "unknown")
endif()

set(ORC_VERSION "${GIT_COMMIT_HASH}")
message(STATUS "Decode Orc Version: ${ORC_VERSION}")

# Configure version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

# Build options
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_GUI "Build Qt6 GUI applications" ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Core library
add_subdirectory(core)

# CLI tools
add_subdirectory(cli)

# GUI (optional)
if(BUILD_GUI)
    # Try to find QtNodes first (e.g., when building in Flatpak)
    find_package(QtNodes QUIET)
    
    if(NOT QtNodes_FOUND)
        # Fetch QtNodes for node graph editing
        include(FetchContent)
        FetchContent_Declare(
            QtNodes
            GIT_REPOSITORY https://github.com/paceholder/nodeeditor.git
            GIT_TAG master
            GIT_SHALLOW TRUE
        )
        
        set(USE_QT6 ON CACHE BOOL "Use Qt6 for QtNodes" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "Disable QtNodes tests" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "Disable QtNodes examples" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build QtNodes as static lib" FORCE)
        
        message(STATUS "Fetching QtNodes...")
        FetchContent_MakeAvailable(QtNodes)
    else()
        message(STATUS "Using system QtNodes")
    endif()
    
    # Define NODE_EDITOR_STATIC for static linking
    add_compile_definitions(NODE_EDITOR_STATIC)
    
    add_subdirectory(gui)
endif()
