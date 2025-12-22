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
    guiproject.cpp
    guiproject.h
    orcgraphmodel.cpp
    orcgraphmodel.h
    orcgraphicsscene.cpp
    orcgraphicsscene.h
    orcgraphicsview.cpp
    orcgraphicsview.h
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
    
    # Inspection dialog
    inspection_dialog.cpp
    inspection_dialog.h
)

target_link_libraries(orc-gui PRIVATE 
    orc-core 
    Qt6::Core 
    Qt6::Widgets
    QtNodes
)

target_compile_definitions(orc-gui PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE
)

target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_BINARY_DIR}/generated
)

# Install target
install(TARGETS orc-gui
    RUNTIME DESTINATION bin
)
