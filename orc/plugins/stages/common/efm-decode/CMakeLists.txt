# Shared EFM decode library (t-values -> CD audio / ECMA-130 sector data)
# used by the efm_sink and efm_audio_decode stage plugins.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Simon Inns

file(GLOB_RECURSE EFM_DECODE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

if(NOT MSVC)
    # Performance-critical pipeline: force -O2 even in Debug. The EFM sources
    # carry a lot of signed/unsigned comparison noise; suppress only those
    # categories with targeted flags instead of the blanket "-w" (which hid all
    # diagnostics, including genuinely useful ones).
    set_source_files_properties(${EFM_DECODE_SOURCES} PROPERTIES
        COMPILE_FLAGS "-O2 -Wno-sign-compare -Wno-sign-conversion")
endif()

add_library(orc-efm-decode STATIC
    ${EFM_DECODE_SOURCES}
)

# P-11: make the log-level choice deliberate. This library's decode path runs
# ~26 million frames per stereo disc, with DEBUG/TRACE log sites in the per-frame
# and per-byte inner loops. A compiled-in-but-runtime-disabled spdlog call still
# costs ~10 ns (level check + argument marshalling), which measures in the tens
# of seconds across a full disc. So DEBUG and TRACE are deliberately compiled out
# here (SPDLOG_LEVEL_INFO) - the opposite trade-off to orc-core/orc-gui - keeping
# INFO-level statistics while paying nothing for the hot-loop diagnostics. Raise
# this to DEBUG/TRACE temporarily when diagnosing the pipeline internals.
target_compile_definitions(orc-efm-decode PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)

# Consumers include the entry point as "efm-decode/efm_processor.h" so the
# shared-library provenance is visible at every include site. The internal
# subtrees use flat includes ("decoders.h", "section.h", ...), so they must be
# PUBLIC too: efm_processor.h itself includes the decoder and writer headers.
target_include_directories(orc-efm-decode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/decoders
        ${CMAKE_CURRENT_SOURCE_DIR}/writers
        ${CMAKE_CURRENT_SOURCE_DIR}/efm-lib
    PRIVATE
        ${EZPWD_INCLUDE_DIR}
)

set_target_properties(orc-efm-decode PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# The plugin SDK provides the <orc/stage/...> contract headers and the
# spdlog/fmt usage requirements needed by <orc/stage/logging.h>.
target_link_libraries(orc-efm-decode PUBLIC orc-plugin-sdk)
