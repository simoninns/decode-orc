# Section: CLI build configuration
# Defines the orc-cli executable and associated build settings
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
)

# Section: Architecture enforcement (MVP)
# CLI uses presenter and common layers; prevents direct core linkage
target_link_libraries(orc-cli PRIVATE 
    orc-presenters
    orc-common
)

# Section: Public include paths for CLI
target_include_directories(orc-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/presenters/include
    ${CMAKE_SOURCE_DIR}/orc/public
    ${CMAKE_SOURCE_DIR}/orc/common/include
    ${CMAKE_BINARY_DIR}/generated
)

# Section: Build-time flags for MVP enforcement
target_compile_definitions(orc-cli PRIVATE
    ORC_CLI_BUILD
)

# Section: Install rules
install(TARGETS orc-cli
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)

# On Windows, install vcpkg runtime dependencies
if(WIN32)
    install(CODE "
        # Copy vcpkg runtime DLLs for orc-cli
        set(VCPKG_INSTALLED_DIR \"\${VCPKG_INSTALLED_DIR}\")
        set(VCPKG_TARGET_TRIPLET \"\${VCPKG_TARGET_TRIPLET}\")
        if(NOT VCPKG_INSTALLED_DIR OR VCPKG_INSTALLED_DIR STREQUAL \"\")
            set(VCPKG_INSTALLED_DIR \"\${CMAKE_BINARY_DIR}/vcpkg_installed\")
        endif()
        if(NOT VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL \"\")
            set(VCPKG_TARGET_TRIPLET \"x64-windows\")
        endif()
        set(VCPKG_BIN_DIR \"\\\${VCPKG_INSTALLED_DIR}/\\\${VCPKG_TARGET_TRIPLET}/bin\")
        if(EXISTS \"\\\${VCPKG_BIN_DIR}\")
            message(STATUS \"Installing vcpkg runtime dependencies for orc-cli...\")
            
            # List of required DLL patterns
            set(DLL_PATTERNS
                \"spdlog.dll\"
                \"yaml-cpp.dll\"
                \"sqlite3.dll\"
                \"libpng*.dll\"
                \"zlib*.dll\"
                \"fmt.dll\"
            )
            
            foreach(DLL_PATTERN \\\${DLL_PATTERNS})
                file(GLOB DLL_FILES \"\\\${VCPKG_BIN_DIR}/\\\${DLL_PATTERN}\")
                foreach(DLL_FILE \\\${DLL_FILES})
                    get_filename_component(DLL_NAME \"\\\${DLL_FILE}\" NAME)
                    message(STATUS \"  Installing \\\${DLL_NAME}\")
                    file(INSTALL \"\\\${DLL_FILE}\" DESTINATION \"\\\${CMAKE_INSTALL_PREFIX}/bin\")
                endforeach()
            endforeach()
        else()
            message(WARNING \"vcpkg bin directory not found at \\\${VCPKG_BIN_DIR}\")
        endif()
    " COMPONENT Runtime)
endif()
