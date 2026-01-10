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

# Generate platform-specific icons from source PNG
include(${CMAKE_SOURCE_DIR}/cmake/GenerateIcons.cmake)
set(ICON_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")
generate_platform_icons(
    "${CMAKE_SOURCE_DIR}/assets/orc-gui-icon-256.png"
    "${ICON_OUTPUT_DIR}"
)

# GUI application sources
add_executable(orc-gui
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
    
    # Analysis framework GUI components
    analysis/analysis_progress_impl.cpp
    analysis/analysis_progress_impl.h
    analysis/analysis_runner.cpp
    analysis/analysis_runner.h
    analysis/analysis_dialog.cpp
    analysis/analysis_dialog.h
        analysis/vectorscope_dialog.cpp
        analysis/vectorscope_dialog.h
    
    # Inspection dialog
    inspection_dialog.cpp
    inspection_dialog.h
    
    # VBI dialog
    vbidialog.cpp
    vbidialog.h
    
    # Hints dialog
    hintsdialog.cpp
    hintsdialog.h
    
    # Pulldown dialog
    pulldowndialog.cpp
    pulldowndialog.h
    
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

target_link_libraries(orc-gui PRIVATE 
    orc-core 
    Qt6::Core 
    Qt6::Widgets
    QtNodes
)

# Link FFTW3 (needed by chroma decoders for Transform2D/3D)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
target_link_libraries(orc-gui PRIVATE ${FFTW3_LIBRARIES})
target_include_directories(orc-gui PRIVATE ${FFTW3_INCLUDE_DIRS})

target_compile_definitions(orc-gui PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core/stages/ld_sink
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

if(APPLE AND MACOS_ICON_FILE)
    # Configure macOS bundle icon
    set_target_properties(orc-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE orc-gui.icns
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
    set_source_files_properties(${MACOS_ICON_FILE} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(orc-gui PRIVATE ${MACOS_ICON_FILE})
    add_custom_target(generate_macos_icon DEPENDS ${MACOS_ICON_FILE})
    add_dependencies(orc-gui generate_macos_icon)
endif()

# Install target
install(TARGETS orc-gui
    RUNTIME DESTINATION bin
)

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
