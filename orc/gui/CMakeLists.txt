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
    
    # Plot widget
    plotwidget.cpp
    plotwidget.h
    
    # Analysis dialog base class
    analysisdialogbase.cpp
    analysisdialogbase.h
    
    # Dropout analysis dialog
    dropoutanalysisdialog.cpp
    dropoutanalysisdialog.h
    
    # SNR analysis dialog
    snranalysisdialog.cpp
    snranalysisdialog.h
    
    # Burst level analysis dialog
    burstlevelanalysisdialog.cpp
    burstlevelanalysisdialog.h
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

# Install target
install(TARGETS orc-gui
    RUNTIME DESTINATION bin
)
