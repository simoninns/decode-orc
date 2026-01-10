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
add_executable(orc-gui WIN32
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

# Note: FFTW3 is already linked transitively through orc-core from orc_chroma_decoders

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
    # Generate the .icns file for use by the release packaging workflow
    # The workflow creates its own bundle structure manually
    add_custom_target(generate_macos_icon DEPENDS ${MACOS_ICON_FILE})
    add_dependencies(orc-gui generate_macos_icon)
endif()

# Install target
install(TARGETS orc-gui
    RUNTIME DESTINATION bin
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
    install(CODE "
        # Run windeployqt
        set(WINDEPLOYQT_EXECUTABLE \"${WINDEPLOYQT_EXECUTABLE}\")
        if(EXISTS \"\${WINDEPLOYQT_EXECUTABLE}\")
            message(STATUS \"Running windeployqt from: \${WINDEPLOYQT_EXECUTABLE}\")
            message(STATUS \"Target executable: \${CMAKE_INSTALL_PREFIX}/bin/orc-gui.exe\")
            execute_process(
                COMMAND \"\${WINDEPLOYQT_EXECUTABLE}\"
                    --release 
                    --no-translations 
                    --no-compiler-runtime
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    \"\${CMAKE_INSTALL_PREFIX}/bin/orc-gui.exe\"
                RESULT_VARIABLE WINDEPLOYQT_RESULT
                OUTPUT_VARIABLE WINDEPLOYQT_OUTPUT
                ERROR_VARIABLE WINDEPLOYQT_ERROR
            )
            if(NOT WINDEPLOYQT_RESULT EQUAL 0)
                message(WARNING \"windeployqt failed with result \${WINDEPLOYQT_RESULT}\")
                message(WARNING \"Output: \${WINDEPLOYQT_OUTPUT}\")
                message(WARNING \"Error: \${WINDEPLOYQT_ERROR}\")
            else()
                message(STATUS \"windeployqt completed successfully\")
            endif()
        else()
            message(WARNING \"windeployqt executable not found at \${WINDEPLOYQT_EXECUTABLE}\")
        endif()
        
        # Copy vcpkg runtime DLLs
        set(VCPKG_BIN_DIR \"C:/vcpkg/installed/x64-windows/bin\")
        if(EXISTS \"\${VCPKG_BIN_DIR}\")
            message(STATUS \"Installing vcpkg runtime dependencies...\")
            
            # List of required DLL patterns
            set(DLL_PATTERNS
                \"fftw3.dll\"
                \"fftw3f.dll\"
                \"fftw3l.dll\"
                \"spdlog.dll\"
                \"yaml-cpp.dll\"
                \"fmt.dll\"
                \"libpng16.dll\"
                \"zlib1.dll\"
                \"sqlite3.dll\"
            )
            
            foreach(DLL_PATTERN \${DLL_PATTERNS})
                file(GLOB DLL_FILES \"\${VCPKG_BIN_DIR}/\${DLL_PATTERN}\")
                foreach(DLL_FILE \${DLL_FILES})
                    get_filename_component(DLL_NAME \"\${DLL_FILE}\" NAME)
                    message(STATUS \"  Installing \${DLL_NAME}\")
                    file(INSTALL \"\${DLL_FILE}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\")
                endforeach()
            endforeach()
            
            # Copy FFmpeg DLLs (with version numbers)
            file(GLOB FFMPEG_DLLS 
                \"\${VCPKG_BIN_DIR}/avcodec-*.dll\"
                \"\${VCPKG_BIN_DIR}/avformat-*.dll\"
                \"\${VCPKG_BIN_DIR}/avutil-*.dll\"
                \"\${VCPKG_BIN_DIR}/swscale-*.dll\"
                \"\${VCPKG_BIN_DIR}/swresample-*.dll\"
            )
            foreach(DLL_FILE \${FFMPEG_DLLS})
                get_filename_component(DLL_NAME \"\${DLL_FILE}\" NAME)
                message(STATUS \"  Installing \${DLL_NAME}\")
                file(INSTALL \"\${DLL_FILE}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\")
            endforeach()
            
            # Copy codec DLLs
            file(GLOB CODEC_DLLS
                \"\${VCPKG_BIN_DIR}/libx264-*.dll\"
                \"\${VCPKG_BIN_DIR}/libx265.dll\"
                \"\${VCPKG_BIN_DIR}/vpx*.dll\"
            )
            foreach(DLL_FILE \${CODEC_DLLS})
                if(EXISTS \"\${DLL_FILE}\")
                    get_filename_component(DLL_NAME \"\${DLL_FILE}\" NAME)
                    message(STATUS \"  Installing \${DLL_NAME}\")
                    file(INSTALL \"\${DLL_FILE}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\")
                endif()
            endforeach()
            
            # Copy additional codec dependencies
            file(GLOB ADDITIONAL_DLLS \"\${VCPKG_BIN_DIR}/*.dll\")
            foreach(DLL_FILE \${ADDITIONAL_DLLS})
                get_filename_component(DLL_NAME \"\${DLL_FILE}\" NAME)
                if(DLL_NAME MATCHES \"(opus|vorbis|ogg|webp|dav1d|aom|svt)\")
                    message(STATUS \"  Installing \${DLL_NAME}\")
                    file(INSTALL \"\${DLL_FILE}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}/bin\")
                endif()
            endforeach()
        else()
            message(WARNING \"vcpkg bin directory not found at \${VCPKG_BIN_DIR}\")
        endif()
    " COMPONENT Runtime)
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
