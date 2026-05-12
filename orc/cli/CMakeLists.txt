# Section: CLI build configuration
# Defines the orc-cli executable and associated build settings
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
    command_plugins.cpp
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

if(APPLE)
    set_target_properties(orc-cli PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX AND NOT WIN32)
    set_target_properties(orc-cli PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib")
endif()

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

if(BUILD_UNIT_TESTS)
    add_test(
        NAME CLI.HelpShowsProcessOnlyCommand
        COMMAND $<TARGET_FILE:orc-cli> --help
    )
    set_tests_properties(CLI.HelpShowsProcessOnlyCommand PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "--process"
    )

    add_test(
        NAME CLI.HelpShowsPluginsSubcommand
        COMMAND $<TARGET_FILE:orc-cli> --help
    )
    set_tests_properties(CLI.HelpShowsPluginsSubcommand PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "plugins"
    )

    add_test(
        NAME CLI.HelpShowsSafeCorePluginsOption
        COMMAND $<TARGET_FILE:orc-cli> --help
    )
    set_tests_properties(CLI.HelpShowsSafeCorePluginsOption PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "--safe-core-plugins"
    )

    add_test(
        NAME CLI.PluginsListWithSafeCorePluginsBeforeSubcommandSucceeds
        COMMAND $<TARGET_FILE:orc-cli> plugins --safe-core-plugins list
    )
    set_tests_properties(CLI.PluginsListWithSafeCorePluginsBeforeSubcommandSucceeds PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "Registry path:"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home-safe-before;HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home-safe-before"
    )

    add_test(
        NAME CLI.PluginsListWithSafeCorePluginsAfterSubcommandSucceeds
        COMMAND $<TARGET_FILE:orc-cli> plugins list --safe-core-plugins
    )
    set_tests_properties(CLI.PluginsListWithSafeCorePluginsAfterSubcommandSucceeds PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "Registry path:"
        ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home-safe-after;HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home-safe-after"
    )

    add_test(
        NAME CLI.PluginsListSucceeds
        COMMAND $<TARGET_FILE:orc-cli> plugins list
    )
    set_tests_properties(CLI.PluginsListSucceeds PROPERTIES
        LABELS "unit;cli"
        PASS_REGULAR_EXPRESSION "Registry path:"
    )

    add_test(
        NAME CLI.PluginsAddMissingPathFails
        COMMAND $<TARGET_FILE:orc-cli> plugins add
    )
    set_tests_properties(CLI.PluginsAddMissingPathFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.PluginsRemoveMissingIdFails
        COMMAND $<TARGET_FILE:orc-cli> plugins remove
    )
    set_tests_properties(CLI.PluginsRemoveMissingIdFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.PluginsEnableMissingIdFails
        COMMAND $<TARGET_FILE:orc-cli> plugins enable
    )
    set_tests_properties(CLI.PluginsEnableMissingIdFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.PluginsDisableMissingIdFails
        COMMAND $<TARGET_FILE:orc-cli> plugins disable
    )
    set_tests_properties(CLI.PluginsDisableMissingIdFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.PluginsUnknownSubcommandFails
        COMMAND $<TARGET_FILE:orc-cli> plugins frobnicate
    )
    set_tests_properties(CLI.PluginsUnknownSubcommandFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.RejectsPreviewExportOption
        COMMAND $<TARGET_FILE:orc-cli> ${CMAKE_SOURCE_DIR}/test-projects/ntsc-stacking-comp.orcprj --preview-export
    )
    set_tests_properties(CLI.RejectsPreviewExportOption PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )

    add_test(
        NAME CLI.ProcessNoSinksFails
        COMMAND $<TARGET_FILE:orc-cli> ${CMAKE_CURRENT_SOURCE_DIR}/test-data/no-sinks.orcprj --process
    )
    set_tests_properties(CLI.ProcessNoSinksFails PROPERTIES
        LABELS "unit;cli"
        WILL_FAIL TRUE
    )
endif()
