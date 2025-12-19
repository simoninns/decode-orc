# CLI tools

# orc-cli: Execute DAG pipelines on TBC files
add_executable(orc-cli orc_cli.cpp)
target_link_libraries(orc-cli PRIVATE orc-core)
target_include_directories(orc-cli PRIVATE 
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_BINARY_DIR}/generated
)

