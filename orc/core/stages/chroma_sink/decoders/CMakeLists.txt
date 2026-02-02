# Chroma Decoder Library (Pure C++17)

# Define decoder library sources
set(DECODER_SOURCES
    decoder.cpp
    paldecoder.cpp
    palcolour.cpp
    transformpal.cpp
    transformpal2d.cpp
    transformpal3d.cpp
    ntscdecoder.cpp
    comb.cpp
    monodecoder.cpp
    componentframe.cpp
    framecanvas.cpp
    outputwriter.cpp
)

# Create static library
add_library(orc_chroma_decoders STATIC
    ${DECODER_SOURCES}
)

# Include directories
target_include_directories(orc_chroma_decoders
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/orc/core/include  # For ORC logging and tbc_metadata
        ${CMAKE_SOURCE_DIR}/orc/view-types  # For VideoParameters and other view types
        ${CMAKE_SOURCE_DIR}/orc/common/include  # For common types
)

# Set C++ standard
set_target_properties(orc_chroma_decoders PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Define M_PI for MSVC (must be defined before <cmath> is included)
if(MSVC)
    target_compile_definitions(orc_chroma_decoders PRIVATE _USE_MATH_DEFINES)
endif()

# Link FFTW3 (required by transformpal2d.cpp and transformpal3d.cpp)
find_package(FFTW3 CONFIG QUIET)
if(TARGET FFTW3::fftw3)
    target_link_libraries(orc_chroma_decoders PUBLIC FFTW3::fftw3)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
    target_link_libraries(orc_chroma_decoders PUBLIC PkgConfig::FFTW3)
endif()

# Link spdlog (required by logging.h)
find_package(spdlog REQUIRED)
target_link_libraries(orc_chroma_decoders PUBLIC spdlog::spdlog)

# Link orc-common (for shared types)
target_link_libraries(orc_chroma_decoders PUBLIC orc-common)
