cmake_minimum_required(VERSION 3.20)
project(ld-disc-crapper VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt5
find_package(Qt5 REQUIRED COMPONENTS Core Sql)

# Define version info for legacy library
add_compile_definitions(APP_BRANCH="ld-disc-crapper" APP_COMMIT="1.0")

# Use the legacy library
add_subdirectory(../library ${CMAKE_BINARY_DIR}/legacy-lib)

# Build the disc crapper tool
add_executable(ld-disc-crapper
    main.cpp
)

target_link_libraries(ld-disc-crapper
    Qt5::Core
    lddecode-library
)

target_include_directories(ld-disc-crapper PRIVATE
    ../library/tbc
)

target_compile_options(ld-disc-crapper PRIVATE
    -Wall -Wextra
)
