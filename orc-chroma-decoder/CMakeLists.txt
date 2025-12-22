cmake_minimum_required(VERSION 3.20)
project(orc-chroma-decoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Get git commit hash for version
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_BRANCH "unknown")
endif()

# For M_PI constant
add_compile_definitions(_USE_MATH_DEFINES)
add_compile_definitions(APP_BRANCH="${GIT_BRANCH}")
add_compile_definitions(APP_COMMIT="${GIT_COMMIT_HASH}")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW REQUIRED IMPORTED_TARGET fftw3)

# TBC library (static)
add_library(tbc STATIC
    lib/tbc/dropouts.cpp
    lib/tbc/filters.cpp
    lib/tbc/sqliteio.cpp
    lib/tbc/lddecodemetadata.cpp
    lib/tbc/logging.cpp
    lib/tbc/navigation.cpp
    lib/tbc/sourceaudio.cpp
    lib/tbc/sourcevideo.cpp
    lib/tbc/vbidecoder.cpp
    lib/tbc/videoiddecoder.cpp
    lib/tbc/vitcdecoder.cpp
)

target_include_directories(tbc PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/filter
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/tbc
)

target_link_libraries(tbc PRIVATE Qt6::Core Qt6::Sql)

# Chroma decoder library (static)
add_library(chroma STATIC
    src/decoder.cpp
    src/decoderpool.cpp
    src/comb.cpp
    src/componentframe.cpp
    src/framecanvas.cpp
    src/outputwriter.cpp
    src/palcolour.cpp
    src/sourcefield.cpp
    src/transformpal.cpp
    src/transformpal2d.cpp
    src/transformpal3d.cpp
    src/monodecoder.cpp
)

set_target_properties(chroma PROPERTIES AUTOMOC ON)

target_include_directories(chroma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(chroma PRIVATE ${FFTW_INCLUDE_DIRS})

target_link_libraries(chroma PRIVATE 
    Qt6::Core 
    PkgConfig::FFTW
    tbc
)

# Main executable
add_executable(orc-chroma-decoder
    src/decoder.cpp
    src/decoderpool.cpp
    src/main.cpp
    src/monodecoder.cpp
    src/ntscdecoder.cpp
    src/paldecoder.cpp
)

set_target_properties(orc-chroma-decoder PROPERTIES AUTOMOC ON)

target_include_directories(orc-chroma-decoder PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FFTW_INCLUDE_DIRS}
)

target_link_libraries(orc-chroma-decoder PRIVATE 
    Qt6::Core 
    tbc 
    chroma
)

# Installation
install(TARGETS orc-chroma-decoder RUNTIME DESTINATION bin)
