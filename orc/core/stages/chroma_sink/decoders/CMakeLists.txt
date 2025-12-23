# Chroma Decoder Library (Qt6-based - to be converted in Step 3)

# Find required Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)

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

# Link Qt6 libraries
target_link_libraries(orc_chroma_decoders
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
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
    AUTOMOC OFF
)
