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

# =============================================================================
# MVP Architecture Validation
# =============================================================================
# Add custom target to check MVP violations
# Run with: cmake --build build --target check-mvp
# Or manually: ./check_mvp_architecture.sh
add_custom_target(check-mvp
    COMMAND ${CMAKE_SOURCE_DIR}/check_mvp_architecture.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Validating MVP architecture boundaries..."
    VERBATIM
)

# Fast check without compiler tests (for CI/quick validation)
add_custom_target(check-mvp-fast
    COMMAND ${CMAKE_SOURCE_DIR}/check_mvp_architecture.sh --skip-compiler-tests
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

# Doxygen documentation (optional)
if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        # Set the Doxyfile location (assuming it's in the project root)
        set(DOXYFILE_IN ${CMAKE_SOURCE_DIR}/Doxyfile)
        
        if(EXISTS ${DOXYFILE_IN})
            # Add a custom target to run Doxygen
            add_custom_target(docs
                COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_IN}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Generating API documentation with Doxygen"
                VERBATIM
            )
            message(STATUS "Doxygen documentation target 'docs' configured")
        else()
            message(WARNING "Doxyfile not found at ${DOXYFILE_IN}")
        endif()
    else()
        message(WARNING "Doxygen not found - documentation will not be built")
    endif()
endif()

# CPack configuration for installer generation
set(CPACK_PACKAGE_NAME "Decode Orc")
set(CPACK_PACKAGE_VENDOR "Simon Inns")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform orchestration framework for LaserDisc and tape decoding workflows")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DecodeOrc")
set(CPACK_PACKAGE_EXECUTABLES "orc-gui;Decode Orc GUI" "orc-cli;Decode Orc CLI")

# License and readme
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# WiX specific settings for MSI generation on Windows
if(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_UPGRADE_GUID "8A4B5C6D-7E8F-9A0B-1C2D-3E4F5A6B7C8D")
    set(CPACK_WIX_PRODUCT_GUID "*") # Generate new GUID for each version
    # WiX requires a .ico file for the product icon. Use generated icon if available.
    if(EXISTS "${CMAKE_BINARY_DIR}/orc/gui/icons/orc-gui.ico")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_BINARY_DIR}/orc/gui/icons/orc-gui.ico")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/assets/orc-gui-icon.ico")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/assets/orc-gui-icon.ico")
    endif()
    set(CPACK_WIX_LICENSE_RTF "${CMAKE_BINARY_DIR}/LICENSE.rtf")
    
    # Optional: Custom WiX UI images (only if they exist)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/wix-banner.bmp")
        set(CPACK_WIX_UI_BANNER "${CMAKE_SOURCE_DIR}/assets/wix-banner.bmp")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/wix-dialog.bmp")
        set(CPACK_WIX_UI_DIALOG "${CMAKE_SOURCE_DIR}/assets/wix-dialog.bmp")
    endif()
    
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "Decode Orc")
    
    # Create start menu shortcuts
    set(CPACK_PACKAGE_EXECUTABLES "orc-gui;Decode Orc")
    
    # Add to Programs and Features
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://github.com/simoninns/decode-orc")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://simoninns.github.io/decode-orc-docs/")
    
    # Generate RTF license from plain text LICENSE file
    # This will be done in the GitHub Actions workflow
endif()

include(CPack)
