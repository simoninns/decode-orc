# CLI tools

# orc-process: Execute DAG pipelines on TBC files
add_executable(orc-process orc_process.cpp)
target_link_libraries(orc-process PRIVATE orc-core)
target_include_directories(orc-process PRIVATE ${CMAKE_SOURCE_DIR}/core/include)

