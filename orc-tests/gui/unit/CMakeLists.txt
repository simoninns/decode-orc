cmake_minimum_required(VERSION 3.20)

project(orc-gui-unit-tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)
find_package(Qt6 COMPONENTS Core Widgets Test QUIET)
find_package(QtNodes REQUIRED)

if(NOT Qt6_FOUND)
    message(WARNING "Qt6 not found, skipping GUI tests")
    return()
endif()

if(NOT TARGET orc-gui-lib)
    message(WARNING "orc-gui-lib target not found, skipping GUI tests")
    return()
endif()

# Enable Qt's MOC (Meta-Object Compiler) for test files if needed
set(CMAKE_AUTOMOC ON)

# This is already included in the top-level CMakeLists so does not need to be included here
#enable_testing()

include(GoogleTest)

function(orc_add_gui_unit_tests target labels)
    set(options OFFSCREEN)
    set(oneValueArgs)
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(ORC_GUI_TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(
        ${target}
        gui_test_logging_stub.cpp
        ${ORC_GUI_TEST_SOURCES}
    )
    target_link_libraries(
        ${target}
        PRIVATE
            orc-gui-lib
            GTest::gmock_main
            Qt6::Core
            Qt6::Widgets
            Qt6::Test
            ${ORC_GUI_TEST_LIBRARIES}
    )
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    set(test_properties LABELS "gui;${labels}")
    if(ORC_GUI_TEST_OFFSCREEN)
        list(APPEND test_properties ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    gtest_discover_tests(${target} PROPERTIES ${test_properties})
endfunction()

set(QTNODES_TARGET ${QtNodes_LIBRARIES})
if(NOT QTNODES_TARGET)
    set(QTNODES_TARGET QtNodes::QtNodes)
endif()

# Phase 1: Pure helpers (no MVP dependencies)
# These test files test helper sources with no dependencies on orc-core.
# OFFSCREEN is required because theme_manager_test.cpp creates QApplication for
# resolve() tests; setting QT_QPA_PLATFORM=offscreen ensures CI (headless) passes.
orc_add_gui_unit_tests(
        orc-gui-unit-tests-helpers
        "unit;gui-logic"
        OFFSCREEN
        SOURCES
            field_frame_presentation_test.cpp
            line_navigation_mapper_test.cpp
            theme_manager_test.cpp
            node_type_helper_port_test.cpp
            vectorscope_geometry_test.cpp
)

# Phase 3: GUIProject model tests (with presenter seam)
orc_add_gui_unit_tests(
    orc-gui-unit-tests-model
    "gui-model"
    SOURCES
        gui_project_test.cpp
        orc_graph_model_test.cpp
        render_coordinator_test.cpp
)

# Phase 6: Offscreen widget test harness baseline
# Phase 7: StageParameterDialog round-trip tests
# Phase 8: ConfigDialogBase subclass rule tests
# Phase 9: Analysis dialog smoke tests
orc_add_gui_unit_tests(
    orc-gui-unit-tests-widget-harness
    "gui-widget"
    OFFSCREEN
    SOURCES
        widget_harness_smoke_test.cpp
        config_dialog_base_test.cpp
        stage_parameter_dialog_test.cpp
        analysis_dialog_smoke_test.cpp
)
