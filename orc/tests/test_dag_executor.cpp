/*
 * File:        test_dag_executor.cpp
 * Module:      orc-tests
 * Purpose:     Dag Executor test suite
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "dag_executor.h"
#include <cassert>
#include <iostream>

using namespace orc;

// Mock stage for testing
class MockStage : public DAGStage {
public:
    MockStage(std::string name, std::string version, size_t inputs, size_t outputs)
        : name_(std::move(name)), version_(std::move(version)), 
          input_count_(inputs), output_count_(outputs) {}
    
    std::string version() const override { return version_; }
    
    NodeTypeInfo get_node_type_info() const override {
        NodeType type = NodeType::TRANSFORM;
        if (input_count_ == 0) type = NodeType::SOURCE;
        else if (output_count_ == 0) type = NodeType::SINK;
        else if (output_count_ > 1) type = NodeType::SPLITTER;
        
        return NodeTypeInfo{
            type,
            name_,
            name_,
            "Mock stage for testing",
            static_cast<uint32_t>(input_count_), static_cast<uint32_t>(input_count_),
            static_cast<uint32_t>(output_count_), static_cast<uint32_t>(output_count_),
            true
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, std::string>& parameters
    ) override {
        // Create mock output artifact
        Provenance prov;
        prov.stage_name = name_;
        prov.stage_version = version_;
        prov.parameters = parameters;
        
        ArtifactID id("mock_artifact_" + name_);
        
        // For this test, we'll return a simple mock
        // In real implementation, this would be a concrete artifact type
        return {};  // Simplified for now
    }
    
    size_t required_input_count() const override { return input_count_; }
    size_t output_count() const override { return output_count_; }
    
private:
    std::string name_;
    std::string version_;
    size_t input_count_;
    size_t output_count_;
};

void test_dag_construction() {
    DAG dag;
    
    DAGNode node;
    node.node_id = "node1";
    node.stage = std::make_shared<MockStage>("TestStage", "1.0", 0, 1);
    
    dag.add_node(node);
    dag.set_output_nodes({"node1"});
    
    assert(dag.nodes().size() == 1);
    assert(dag.output_nodes().size() == 1);
    
    std::cout << "test_dag_construction: PASSED\n";
}

void test_dag_validation_cycle() {
    DAG dag;
    
    auto stage = std::make_shared<MockStage>("TestStage", "1.0", 1, 1);
    
    DAGNode node1;
    node1.node_id = "node1";
    node1.stage = stage;
    node1.input_node_ids = {"node2"};
    
    DAGNode node2;
    node2.node_id = "node2";
    node2.stage = stage;
    node2.input_node_ids = {"node1"};  // Cycle!
    
    dag.add_node(node1);
    dag.add_node(node2);
    dag.set_output_nodes({"node1"});
    
    assert(!dag.validate());
    auto errors = dag.get_validation_errors();
    assert(!errors.empty());
    
    std::cout << "test_dag_validation_cycle: PASSED\n";
}

void test_dag_validation_missing_dependency() {
    DAG dag;
    
    DAGNode node;
    node.node_id = "node1";
    node.stage = std::make_shared<MockStage>("TestStage", "1.0", 1, 1);
    node.input_node_ids = {"nonexistent"};
    
    dag.add_node(node);
    dag.set_output_nodes({"node1"});
    
    assert(!dag.validate());
    auto errors = dag.get_validation_errors();
    assert(!errors.empty());
    
    std::cout << "test_dag_validation_missing_dependency: PASSED\n";
}

int main() {
    std::cout << "Running DAGExecutor tests...\n";
    
    test_dag_construction();
    test_dag_validation_cycle();
    test_dag_validation_missing_dependency();
    
    std::cout << "All DAGExecutor tests passed!\n";
    return 0;
}
