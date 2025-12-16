# GUI application placeholder
# Requires Qt6

find_package(Qt6 COMPONENTS Core Widgets QUIET)

if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found, skipping GUI build")
    return()
endif()

# Example: main GUI application
# add_executable(orc-gui main.cpp)
# target_link_libraries(orc-gui PRIVATE orc-core Qt6::Core Qt6::Widgets)
