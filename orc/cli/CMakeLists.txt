# CLI tools

# orc-cli: Execute DAG pipelines on TBC files
add_executable(orc-cli 
    orc_cli.cpp
    command_process.cpp
    command_analyze_field_mapping.cpp
)
target_link_libraries(orc-cli PRIVATE orc-core)
target_include_directories(orc-cli PRIVATE 
    ${CMAKE_SOURCE_DIR}/orc/core/include
    ${CMAKE_SOURCE_DIR}/orc/core
    ${CMAKE_BINARY_DIR}/generated
)

# Link FFTW3 (needed by chroma decoders for Transform2D/3D)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
target_link_libraries(orc-cli PRIVATE ${FFTW3_LIBRARIES})
target_include_directories(orc-cli PRIVATE ${FFTW3_INCLUDE_DIRS})

