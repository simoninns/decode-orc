cmake_minimum_required(VERSION 3.20)

project(orc-core-unit-tests)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)

# This is already included in the top-level cmakelist so does not need to be included here
#enable_testing()

add_executable(
        orc-core-unit-tests

        stages/daphne_vbi_sink/daphne_vbi_writer_util_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_deps_test.cpp
        stages/daphne_vbi_sink/daphne_vbi_sink_stage_test.cpp
)

target_link_libraries(
        orc-core-unit-tests
        orc-core
        GTest::gtest_main
        GTest::gmock_main
)

include(GoogleTest)
gtest_discover_tests(orc-core-unit-tests)