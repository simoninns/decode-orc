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
)

# Set C++ standard
set_target_properties(orc_chroma_decoders PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Link FFTW3 (required by transformpal2d.cpp and transformpal3d.cpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
target_link_libraries(orc_chroma_decoders PUBLIC ${FFTW3_LIBRARIES})
target_include_directories(orc_chroma_decoders PUBLIC ${FFTW3_INCLUDE_DIRS})
