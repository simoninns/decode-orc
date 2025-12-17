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
    fieldpreviewwidget.cpp
    fieldpreviewwidget.h
    dagviewerwidget.cpp
    dagviewerwidget.h
    dagnodeeditdialog.cpp
    dagnodeeditdialog.h
    stageparameterdialog.cpp
    stageparameterdialog.h
)

target_link_libraries(orc-gui PRIVATE 
    orc-core 
    Qt6::Core 
    Qt6::Widgets
)

target_include_directories(orc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

# Install target
install(TARGETS orc-gui
    RUNTIME DESTINATION bin
)
