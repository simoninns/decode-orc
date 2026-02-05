# GUI application
# Requires Qt6

find_package(Qt6 COMPONENTS Core Widgets QUIET)

if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found, skipping GUI build")
    return()
endif()

# Enable Qt's MOC (Meta-Object Compiler)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

# Generate platform-specific icons from source PNG
include(${CMAKE_SOURCE_DIR}/cmake/GenerateIcons.cmake)
set(ICON_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")
generate_platform_icons(
    "${CMAKE_SOURCE_DIR}/assets/orc-gui-icon-256.png"
    "${ICON_OUTPUT_DIR}"
)

# =============================================================================
# GUI Application Sources
# =============================================================================

add_executable(orc-gui WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow_coordinator_callbacks.cpp
    render_coordinator.cpp
    render_coordinator.h
    guiproject.cpp
    guiproject.h
    orcgraphmodel.cpp
    orcgraphmodel.h
    orcgraphicsscene.cpp
    orcgraphicsscene.h
    orcgraphicsview.cpp
    orcgraphicsview.h
    orcnodepainter.cpp
    orcnodepainter.h
    fieldpreviewwidget.cpp
    fieldpreviewwidget.h
    previewdialog.cpp
    previewdialog.h
    linescopedialog.cpp
    linescopedialog.h
    stageparameterdialog.cpp
    stageparameterdialog.h
    projectpropertiesdialog.cpp
    projectpropertiesdialog.h
    node_type_helper.cpp
    node_type_helper.h
    
    # Field/Frame presentation helpers
    field_frame_presentation.cpp
    field_frame_presentation.h
    
    # Analysis dialogs
    analysis/vectorscope_dialog.cpp
    analysis/vectorscope_dialog.h
    generic_analysis_dialog.cpp
    generic_analysis_dialog.h
    
    # Inspection dialog
    inspection_dialog.cpp
    inspection_dialog.h
    
    # VBI dialog
    vbidialog.cpp
    vbidialog.h
    hintsdialog.cpp
    hintsdialog.h
    
    # NTSC observer dialog
    ntscobserverdialog.cpp
    ntscobserverdialog.h
    
    # Field timing dialog
    fieldtimingdialog.cpp
    fieldtimingdialog.h
    fieldtimingwidget.cpp
    fieldtimingwidget.h
    
    # Plot widget
    plotwidget.cpp
    plotwidget.h
    
    # Analysis dialog base class
    analysisdialogbase.cpp
    analysisdialogbase.h
    
    # Config dialog base class
    configdialogbase.cpp
    configdialogbase.h
    
    # Mask line config dialog
    masklineconfigdialog.cpp
    masklineconfigdialog.h
    
    # FFmpeg preset config dialog
    ffmpegpresetdialog.cpp
    ffmpegpresetdialog.h
    
    # Dropout analysis dialog
    dropoutanalysisdialog.cpp
    dropoutanalysisdialog.h
    
    # Dropout editor dialog
    dropout_editor_dialog.cpp
    dropout_editor_dialog.h
    
    # SNR analysis dialog
    snranalysisdialog.cpp
    snranalysisdialog.h
    
    # Burst level analysis dialog
    burstlevelanalysisdialog.cpp
    burstlevelanalysisdialog.h
    
    # Quality metrics dialog
    qualitymetricsdialog.cpp
    qualitymetricsdialog.h
    
    # Resources
    orc_gui_resources.qrc
)

# =============================================================================
# Link Libraries
# =============================================================================
# The GUI accesses core functionality exclusively through the presenter layer.
# Direct linking to orc-core is intentionally avoided to enforce architectural
# boundaries. The presenter layer handles all conversions between public API
# types and internal core types.
# =============================================================================
if(TARGET QtNodes::QtNodes)
    set(QTNODES_TARGET QtNodes::QtNodes)
elseif(TARGET QtNodes)
    set(QTNODES_TARGET QtNodes)
else()
    message(FATAL_ERROR "QtNodes target not found")
endif()

target_link_libraries(orc-gui PRIVATE 
    orc-common           # Common types shared across all layers
    orc-view-types       # Shared presentation layer data structures
    orc-presenters       # Presenter layer (privately links orc-core)
    Qt6::Core 
    Qt6::Widgets
    ${QTNODES_TARGET}
)

# QtNodes may be provided as a static library (e.g., Nix dev shells).
# Ensure NODE_EDITOR_STATIC is defined for consumers in that case.
if(TARGET ${QTNODES_TARGET})
    get_target_property(QTNODES_IMPORTED ${QTNODES_TARGET} IMPORTED)
    if(QTNODES_IMPORTED)
        get_target_property(QTNODES_LOCATION ${QTNODES_TARGET} IMPORTED_LOCATION)
        if(NOT QTNODES_LOCATION)
            get_target_property(QTNODES_LOCATION ${QTNODES_TARGET} IMPORTED_LOCATION_RELEASE)
        endif()
        if(QTNODES_LOCATION MATCHES "\\.a$")
            target_compile_definitions(orc-gui PRIVATE NODE_EDITOR_STATIC)
        endif()
    else()
        get_target_property(QTNODES_TYPE ${QTNODES_TARGET} TYPE)
        if(QTNODES_TYPE STREQUAL "STATIC_LIBRARY")
            target_compile_definitions(orc-gui PRIVATE NODE_EDITOR_STATIC)
        endif()
    endif()
endif()

# =============================================================================
# Compile Definitions
# =============================================================================
# ORC_GUI_BUILD: Enables compile-time guards in core headers that prevent
#                direct inclusion from GUI code. If GUI code attempts to
#                include a core header, compilation will fail with a message
#                directing the developer to use the appropriate presenter or
#                public API type instead.
# =============================================================================
target_compile_definitions(orc-gui PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
    ORC_GUI_BUILD
)

# =============================================================================
# Include Directories
# =============================================================================
# The GUI has access to:
# - Public API (orc/public): Stable interface types for rendering, analysis, etc.
# - Presenters (orc/presenters/include): Application logic layer
# - Common types (orc/common/include): Types shared across all layers
# - Generated files: Build version information
#
# Core implementation headers (orc/core) are intentionally NOT in the include
# path. This ensures GUI code cannot directly access internal implementation
# details. If you need access to core functionality, add it to the appropriate
# presenter or create a new public API type.
# =============================================================================
target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/public
    ${CMAKE_SOURCE_DIR}/orc/presenters/include
    ${CMAKE_SOURCE_DIR}/orc/common/include
    ${CMAKE_SOURCE_DIR}/orc
    ${CMAKE_BINARY_DIR}/generated
)

# Platform-specific icon configuration
if(WIN32 AND WINDOWS_ICON_FILE)
    # Create Windows resource file for icon
    set(RC_FILE "${CMAKE_CURRENT_BINARY_DIR}/orc-gui.rc")
    file(WRITE ${RC_FILE} "IDI_ICON1 ICON \"${WINDOWS_ICON_FILE}\"\n")
    target_sources(orc-gui PRIVATE ${RC_FILE})
    add_custom_target(generate_windows_icon DEPENDS ${WINDOWS_ICON_FILE})
    add_dependencies(orc-gui generate_windows_icon)
endif()

if(APPLE)
    # Configure the custom Info.plist from template
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        @ONLY
    )
    
    # Set bundle properties for macOS
    set_target_properties(orc-gui PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Decode Orc"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.simoninns.decode-orc"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "orc-gui.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_INFO_STRING "Cross-platform orchestration framework for LaserDisc and tape decoding workflows"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024-2026 Simon Inns"
    )
    
    # Copy the icon into the bundle's Resources directory if available
    if(MACOS_ICON_FILE)
        set_source_files_properties(${MACOS_ICON_FILE} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
        )
        target_sources(orc-gui PRIVATE ${MACOS_ICON_FILE})
        
        add_custom_target(generate_macos_icon DEPENDS ${MACOS_ICON_FILE})
        add_dependencies(orc-gui generate_macos_icon)
    endif()
endif()

# Install target
install(TARGETS orc-gui
    BUNDLE DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION bin COMPONENT Runtime
)

# On Windows, install Qt runtime dependencies and vcpkg DLLs
if(WIN32)
    # Get the Qt bin directory
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    
    # Find windeployqt
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    
    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(WARNING "windeployqt not found - Qt runtime dependencies will not be automatically deployed")
    endif()
    
    # Install script to run windeployqt and copy vcpkg DLLs during installation
    # Configure the install script with necessary variables
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/windows-install-runtime.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/windows-install-runtime.cmake"
        @ONLY
    )
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/windows-install-runtime.cmake" COMPONENT Runtime)
endif()

# Install desktop file
install(FILES io.github.simoninns.decode-orc.desktop
    DESTINATION share/applications
)

# Install application icon
install(FILES ${CMAKE_SOURCE_DIR}/assets/orc-gui-icon-256.png
    DESTINATION share/icons/hicolor/256x256/apps
    RENAME io.github.simoninns.decode-orc.png
)

# Install SVG icon for scalability
if(EXISTS ${CMAKE_SOURCE_DIR}/assets/orc-gui-icon.svg)
    install(FILES ${CMAKE_SOURCE_DIR}/assets/orc-gui-icon.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME io.github.simoninns.decode-orc.svg
    )
endif()
