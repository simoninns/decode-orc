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

# Register tests
add_test(NAME FieldIDTests COMMAND test-field-id)
add_test(NAME DAGExecutorTests COMMAND test-dag-executor)
