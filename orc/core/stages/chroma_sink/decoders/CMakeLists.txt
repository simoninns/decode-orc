# Chroma Decoder Library (Qt6-based - to be converted in Step 3)

# Find required Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Sql)

# Define decoder library sources (excluding main.cpp)
set(DECODER_SOURCES
    decoder.cpp
    decoderpool.cpp
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
    sourcefield.cpp
    outputwriter.cpp
)

# Define encoder library sources
set(ENCODER_SOURCES
    encoder/encoder.cpp
    encoder/ntscencoder.cpp
    encoder/palencoder.cpp
)

# Define TBC library sources (from legacy-tools)
set(TBC_LIB_SOURCES
    lib/tbc/lddecodemetadata.cpp
    lib/tbc/dropouts.cpp
    lib/tbc/filters.cpp
    lib/tbc/logging.cpp
    lib/tbc/navigation.cpp
    lib/tbc/sourceaudio.cpp
    lib/tbc/sourcevideo.cpp
    lib/tbc/sqliteio.cpp
    lib/tbc/vbidecoder.cpp
    lib/tbc/videoiddecoder.cpp
    lib/tbc/vitcdecoder.cpp
)

# Create static library
add_library(orc_chroma_decoders STATIC
    ${DECODER_SOURCES}
    ${ENCODER_SOURCES}
    ${TBC_LIB_SOURCES}
)

# Find SQLite3 (required by lddecodemetadata)
find_package(SQLite3 REQUIRED)

# Link Qt6 libraries and SQLite3
target_link_libraries(orc_chroma_decoders
    PUBLIC
        Qt6::Core
        Qt6::Concurrent
        Qt6::Sql
        SQLite::SQLite3
)

# Add compile definitions for APP_BRANCH and APP_COMMIT (used by logging)
target_compile_definitions(orc_chroma_decoders PRIVATE
    APP_BRANCH="chroma-decoder-integration"
    APP_COMMIT="step2"
)

# Include directories
target_include_directories(orc_chroma_decoders
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/encoder
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/tbc
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/filter
)

# Set C++ standard
set_target_properties(orc_chroma_decoders PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
