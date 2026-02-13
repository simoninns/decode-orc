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
if(APPLE)
    # Embed CLI inside the macOS app bundle so drag-and-drop install contains both tools
    install(TARGETS orc-cli
        RUNTIME DESTINATION "orc-gui.app/Contents/MacOS"
        COMPONENT Runtime
    )
else()
    install(TARGETS orc-cli
        RUNTIME DESTINATION bin
        COMPONENT Runtime
    )
endif()
