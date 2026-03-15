# orc-metadata: TBC metadata reader and writer
#
# This static library owns all metadata I/O for decode-orc.
#   TBCMetadataSqliteReader  — reads .tbc.db (SQLite) files
#   TBCMetadataJsonReader    — reads legacy .tbc.json files directly
#   TBCMetadataWriter        — writes .tbc.db files
#
# Legacy JSON parsing uses LdDecodeMetaData (dropouts/jsonio/lddecodemetadata)
# to parse .tbc.json into C++ structures; no SQLite intermediate step is used.
#
# Dependency direction:  orc-common  <-- orc-metadata  <-- orc-core
# SQLite is an implementation detail, linked PRIVATE.

add_library(orc-metadata STATIC
    tbc_metadata.cpp
    tbc_metadata_writer.cpp
    tbc_metadata_json_reader.cpp
    open_tbc_metadata.cpp
    # Legacy JSON parsing sources
    dropouts.cpp
    jsonio.cpp
    lddecodemetadata.cpp
)

target_include_directories(orc-metadata
    # Public: consumers (orc-core) get tbc_metadata.h and tbc_metadata_writer.h
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/orc/metadata>
        # view-types path is needed because tbc_metadata.h includes orc_source_parameters.h.
        # Mirrors the pattern used in orc/core/CMakeLists.txt.
        ${CMAKE_SOURCE_DIR}/orc/view-types

    # Private: only needed when compiling orc-metadata's own sources
    PRIVATE
        # observation_context_interface.h, observation_schema.h, video_metadata_types.h
        ${CMAKE_SOURCE_DIR}/orc/core/include
        # JSON converter headers live alongside their .cpp files in this directory
        ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(SQLite3 REQUIRED)
find_package(spdlog REQUIRED)

target_link_libraries(orc-metadata
    PUBLIC  orc-common              # field_id.h, common_types.h, spdlog (transitive)
    PRIVATE SQLite::SQLite3         # SQLite is an implementation detail
    PRIVATE spdlog::spdlog          # used by lddecodemetadata sources
)

# Match orc-core's compile-time spdlog filter level per build type.
target_compile_definitions(orc-metadata PRIVATE
    $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG>
    $<$<NOT:$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>,$<CONFIG:MinSizeRel>>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
)

# Install public headers
install(DIRECTORY include/
    DESTINATION include/orc/metadata
    FILES_MATCHING PATTERN "*.h"
)
