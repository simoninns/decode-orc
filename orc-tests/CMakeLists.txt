cmake_minimum_required(VERSION 3.20)

function(orc_fail_if_integration_tests_are_in_unit_tree unit_tree_path)
    file(GLOB_RECURSE _misplaced_integration_tests
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/${unit_tree_path}/*_integration_test.cpp")

    if(_misplaced_integration_tests)
        list(JOIN _misplaced_integration_tests "\n  - " _misplaced_list)
        message(FATAL_ERROR
            "Found integration-style tests in unit tree '${unit_tree_path}':\n"
            "  - ${_misplaced_list}\n"
            "Move these files under an integration test subtree.")
    endif()
endfunction()

orc_fail_if_integration_tests_are_in_unit_tree("orc-tests/core/unit")
orc_fail_if_integration_tests_are_in_unit_tree("orc-tests/gui/unit")

if(BUILD_UNIT_TESTS)
    add_subdirectory(core/unit)
endif()

if(BUILD_INTEGRATION_TESTS)
    add_subdirectory(core/integration)
endif()

if(BUILD_GUI_TESTS)
    add_subdirectory(gui/unit)
endif()

if(BUILD_INTEGRATION_TESTS AND BUILD_GUI_TESTS)
    add_subdirectory(gui/integration)
endif()