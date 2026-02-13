cmake_minimum_required(VERSION 3.20)
project(decode-orc VERSION 0.1.0 LANGUAGES CXX)

# Allow version override for release builds
if(DEFINED PROJECT_VERSION_OVERRIDE)
    set(PROJECT_VERSION "${PROJECT_VERSION_OVERRIDE}")
    string(REPLACE "v" "" PROJECT_VERSION_CLEAN "${PROJECT_VERSION}")
    set(PROJECT_VERSION "${PROJECT_VERSION_CLEAN}")
    
    # Parse the version string (e.g., "1.0.8")
    string(REGEX MATCH "^([0-9]+)\.([0-9]+)\.([0-9]+)" _ "${PROJECT_VERSION}")
    if(CMAKE_MATCH_1)
        set(PROJECT_VERSION_MAJOR "${CMAKE_MATCH_1}")
        set(PROJECT_VERSION_MINOR "${CMAKE_MATCH_2}")
        set(PROJECT_VERSION_PATCH "${CMAKE_MATCH_3}")
    endif()
    
    message(STATUS "Using release version override: ${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Repo root (used for submodules)
get_filename_component(ORC_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Get git commit hash for version
# If PROJECT_VERSION_OVERRIDE is set (e.g., from Nix build), use it
if(DEFINED PROJECT_VERSION_OVERRIDE)
    set(ORC_VERSION "${PROJECT_VERSION_OVERRIDE}")
else()
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
endif()

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
option(BUILD_DOCS "Build Doxygen documentation" OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include MVP enforcement utilities
include(${CMAKE_SOURCE_DIR}/cmake/MVPEnforcement.cmake)

# Common types (shared across all layers)
add_subdirectory(common)

# Core library (internal implementation)
add_subdirectory(core)

# View types (shared data structures for presentation layer)
add_subdirectory(view-types)

# Presenter layer (MVP architecture - bridge between GUI/CLI and core)
add_subdirectory(presenters)

# CLI tools
add_subdirectory(cli)

# GUI (optional)
if(BUILD_GUI)
    # Try to find QtNodes first
    find_package(QtNodes QUIET)

    if(NOT QtNodes_FOUND)
        # Prefer the submodule if present
        set(QTNODES_DIR "${ORC_REPO_ROOT}/external/qtnodes")

        if(EXISTS "${QTNODES_DIR}/CMakeLists.txt")
            set(USE_QT6 ON CACHE BOOL "Use Qt6 for QtNodes" FORCE)
            set(BUILD_TESTING OFF CACHE BOOL "Disable QtNodes tests" FORCE)
            set(BUILD_EXAMPLES OFF CACHE BOOL "Disable QtNodes examples" FORCE)
            set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build QtNodes as static lib" FORCE)

            message(STATUS "Using QtNodes submodule at ${QTNODES_DIR}")
            add_subdirectory("${QTNODES_DIR}" "${CMAKE_BINARY_DIR}/external/qtnodes")
        else()
            message(FATAL_ERROR "QtNodes not found. Please init submodule: git submodule update --init --recursive")
        endif()
    else()
        message(STATUS "Using system QtNodes")
        # Only use NODE_EDITOR_STATIC if not using shared system library
    endif()
    
    # Only define NODE_EDITOR_STATIC for static linking when using submodule
    if(NOT QtNodes_FOUND)
        add_compile_definitions(NODE_EDITOR_STATIC)
    endif()
    
    add_subdirectory(gui)
endif()

# =============================================================================
# MVP Architecture Validation
# =============================================================================
# Add custom target to check MVP violations
# Run with: cmake --build build --target check-mvp
# Or manually: ./cmake/check_mvp_architecture.sh
add_custom_target(check-mvp
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_mvp_architecture.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validating MVP architecture boundaries..."
    VERBATIM
)

# Fast check without compiler tests (for CI/quick validation)
add_custom_target(check-mvp-fast
    COMMAND ${BASH_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/check_mvp_architecture.sh --skip-compiler-tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Quick MVP validation (skip compiler tests)..."
    VERBATIM
)

# Optional: Uncomment to enforce MVP check before every GUI/CLI build
# This adds ~1 second overhead per build but catches violations immediately
if(BUILD_GUI)
    add_dependencies(orc-gui check-mvp-fast)
endif()
add_dependencies(orc-cli check-mvp-fast)

# CPack configuration for installer generation
set(CPACK_PACKAGE_NAME "Decode Orc")
set(CPACK_PACKAGE_VENDOR "Simon Inns")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform orchestration framework for LaserDisc and tape decoding workflows")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DecodeOrc")

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_FILE_NAME "Decode-Orc-${PROJECT_VERSION}-macos")
    set(CPACK_DMG_VOLUME_NAME "Decode Orc ${PROJECT_VERSION}")
endif()

include(CPack)

