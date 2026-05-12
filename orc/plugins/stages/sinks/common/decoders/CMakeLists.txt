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

target_compile_definitions(orc_chroma_decoders PUBLIC
    _USE_MATH_DEFINES
)

# Link FFTW3 (required by transformpal2d.cpp and transformpal3d.cpp)
# Try pkg-config first (more reliable across platforms), then CONFIG mode
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFTW3 IMPORTED_TARGET fftw3)
    if(FFTW3_FOUND)
        target_link_libraries(orc_chroma_decoders PUBLIC PkgConfig::FFTW3)
    else()
        find_package(FFTW3 CONFIG REQUIRED)
        target_link_libraries(orc_chroma_decoders PUBLIC FFTW3::fftw3)
    endif()
else()
    find_package(FFTW3 CONFIG REQUIRED)
    target_link_libraries(orc_chroma_decoders PUBLIC FFTW3::fftw3)
endif()

# Link spdlog (required by logging.h)
find_package(spdlog REQUIRED)
target_link_libraries(orc_chroma_decoders PUBLIC spdlog::spdlog)

# Link orc-common (for shared types)
target_link_libraries(orc_chroma_decoders PUBLIC orc-common)
