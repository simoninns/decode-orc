# CLI tools

# orc-cli: Execute DAG pipelines on TBC files
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
)
target_link_libraries(orc-cli PRIVATE orc-core)
target_include_directories(orc-cli PRIVATE 
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core
    ${CMAKE_BINARY_DIR}/generated
)

# Note: FFTW3 is already linked transitively through orc-core from orc_chroma_decoders

# Install target
install(TARGETS orc-cli
    RUNTIME DESTINATION bin
)
