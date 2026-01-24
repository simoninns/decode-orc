# CLI tools

# orc-cli: Execute DAG pipelines on TBC files
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
)

# =============================================================================
# MVP Architecture - CLI Direct Core Access (Phase 3.6 Status)
# =============================================================================
# ARCHITECTURAL DECISION:
# The CLI tool (orc-cli) is a low-level debugging and batch processing tool
# that intentionally maintains direct core access. This is distinct from the
# GUI application's strict MVP enforcement.
#
# RATIONALE:
# - CLI is a developer/power-user tool for advanced operations
# - Direct core access enables low-level debugging and experimentation
# - No UI layer to protect from complexity - users are expected to understand
# - Batch processing benefits from direct DAG manipulation
#
# FUTURE WORK (Optional):
# CLI could be migrated to use presenters for consistency with GUI, but this
# is not required for MVP architecture goals. The primary goal is protecting
# the GUI layer from core complexity, which Phase 3.6 achieves.
#
# Phase 3.6 Status: INTENTIONAL - CLI is a low-level tool with core access
# =============================================================================
target_link_libraries(orc-cli PRIVATE 
    orc-core  # Intentional direct access for low-level CLI operations
)

target_include_directories(orc-cli PRIVATE 
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core
    ${CMAKE_SOURCE_DIR}/orc/core/stages/ld_sink
    ${CMAKE_SOURCE_DIR}/orc/public  # Access to public API types (MVP allows core -> public dependency)
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(orc-cli PRIVATE
    ORC_CLI_BUILD  # Marks CLI build for potential future enforcement
)

# Note: FFTW3 is already linked transitively through orc-core from orc_chroma_decoders

# Install target
install(TARGETS orc-cli
    RUNTIME DESTINATION bin
)
