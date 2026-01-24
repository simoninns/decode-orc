# CLI tools

# orc-cli: Execute DAG pipelines on TBC files
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
)

# =============================================================================
# MVP Architecture - CLI uses presenters (future work)
# =============================================================================
# Note: CLI currently has direct core access as it's a low-level tool.
# Future work could migrate CLI to use presenters for consistency.
# For now, we set ORC_CLI_BUILD flag but still allow core access.
# =============================================================================
target_link_libraries(orc-cli PRIVATE 
    orc-core
)

target_include_directories(orc-cli PRIVATE 
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core
    ${CMAKE_SOURCE_DIR}/orc/core/stages/ld_sink
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
