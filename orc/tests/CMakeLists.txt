# Unit tests
# Using a simple test framework or Google Test when available

# Test executables (separate binaries to avoid multiple main() issues)
add_executable(test-field-id test_field_id.cpp)
target_link_libraries(test-field-id PRIVATE orc-core)
target_include_directories(test-field-id PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-dag-executor test_dag_executor.cpp)
target_link_libraries(test-dag-executor PRIVATE orc-core)
target_include_directories(test-dag-executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-tbc-io test_tbc_io.cpp)
target_link_libraries(test-tbc-io PRIVATE orc-core)
target_include_directories(test-tbc-io PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-real-tbc-data test_real_tbc_data.cpp)
target_link_libraries(test-real-tbc-data PRIVATE orc-core)
target_include_directories(test-real-tbc-data PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-vits-observer test_vits_observer.cpp)
target_link_libraries(test-vits-observer PRIVATE orc-core)
target_include_directories(test-vits-observer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-vbi-observers test_vbi_observers.cpp)
target_link_libraries(test-vbi-observers PRIVATE orc-core)
target_include_directories(test-vbi-observers PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

add_executable(test-dag-field-renderer test_dag_field_renderer.cpp)
target_link_libraries(test-dag-field-renderer PRIVATE orc-core)
target_include_directories(test-dag-field-renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/include
)

# Register tests
add_test(NAME FieldIDTests COMMAND test-field-id)
add_test(NAME DAGExecutorTests COMMAND test-dag-executor)
add_test(NAME TBCIOTests COMMAND test-tbc-io)
add_test(NAME RealTBCDataTests COMMAND test-real-tbc-data)
add_test(NAME VITSObserverTests COMMAND test-vits-observer)
add_test(NAME VBIObserverTests COMMAND test-vbi-observers)
add_test(NAME DAGFieldRendererTests COMMAND test-dag-field-renderer)
