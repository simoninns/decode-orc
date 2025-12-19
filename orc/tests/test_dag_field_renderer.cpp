/*
 * File:        test_dag_field_renderer.cpp
 * Module:      orc-tests
 * Purpose:     Dag Field Renderer test suite
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dag_field_renderer.h"
#include "dag_executor.h"
#include "field_id.h"
#include "video_field_representation.h"

#include <iostream>
#include <cassert>
#include <memory>
#include <vector>

using namespace orc;

// Mock VideoFieldRepresentation for testing
class MockVideoFieldRepresentation : public VideoFieldRepresentation {
public:
    MockVideoFieldRepresentation(const std::string& name, size_t field_count)
        : VideoFieldRepresentation(
            ArtifactID(name),
            create_mock_provenance()
        )
        , field_count_(field_count)
    {
        // Create mock field data
        for (size_t i = 0; i < field_count; ++i) {
            FieldID id(i);
            field_data_[id] = std::vector<uint16_t>(100 * 50, static_cast<uint16_t>(1000 + i));
        }
    }
    
    FieldIDRange field_range() const override {
        return FieldIDRange{FieldID(0), FieldID(field_count_)};
    }
    
    size_t field_count() const override {
        return field_count_;
    }
    
    bool has_field(FieldID id) const override {
        return field_data_.find(id) != field_data_.end();
    }
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
        if (!has_field(id)) return std::nullopt;
        
        FieldDescriptor desc;
        desc.field_id = id;
        desc.parity = (id.value() % 2 == 0) ? FieldParity::Top : FieldParity::Bottom;
        desc.format = VideoFormat::PAL;
        desc.width = 100;
        desc.height = 50;
        return desc;
    }
    
    const sample_type* get_line(FieldID id, size_t line) const override {
        auto it = field_data_.find(id);
        if (it == field_data_.end() || line >= 50) return nullptr;
        return &it->second[line * 100];
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        auto it = field_data_.find(id);
        if (it == field_data_.end()) return {};
        return it->second;
    }
    
private:
    static Provenance create_mock_provenance() {
        Provenance prov;
        prov.stage_name = "Mock";
        prov.stage_version = "1.0";
        prov.created_at = std::chrono::system_clock::now();
        return prov;
    }
    
    size_t field_count_;
    mutable std::map<FieldID, std::vector<uint16_t>> field_data_;
};

// Mock DAGStage for testing
class MockStage : public DAGStage {
public:
    MockStage(const std::string& name, const std::string& version, 
              size_t input_count, size_t output_count)
        : name_(name), version_(version), 
          input_count_(input_count), output_count_(output_count)
    {}
    
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
        const std::map<std::string, std::string>&) override
    {
        // Pass through or generate mock data
        std::vector<ArtifactPtr> outputs;
        
        if (!inputs.empty()) {
            // Pass through first input (simulating transform)
            outputs.push_back(inputs[0]);
        } else {
            // Generate mock representation (simulating source)
            auto mock_repr = std::make_shared<MockVideoFieldRepresentation>(name_, 10);
            outputs.push_back(mock_repr);
        }
        
        return outputs;
    }
    
    size_t required_input_count() const override { return input_count_; }
    size_t output_count() const override { return output_count_; }
    
private:
    std::string name_;
    std::string version_;
    size_t input_count_;
    size_t output_count_;
};

void test_dag_field_renderer_basic() {
    std::cout << "=== Testing DAGFieldRenderer Basic Functionality ===" << std::endl;
    
    // Create a simple DAG: Source -> Transform -> Sink
    auto dag = std::make_shared<DAG>();
    
    // Source node
    DAGNode source_node;
    source_node.node_id = "SOURCE_0";
    source_node.stage = std::make_shared<MockStage>("Source", "1.0", 0, 1);
    dag->add_node(source_node);
    
    // Transform node
    DAGNode transform_node;
    transform_node.node_id = "transform_1";
    transform_node.stage = std::make_shared<MockStage>("Transform", "1.0", 1, 1);
    transform_node.input_node_ids = {"SOURCE_0"};
    dag->add_node(transform_node);
    
    // Sink node
    DAGNode sink_node;
    sink_node.node_id = "SINK_0";
    sink_node.stage = std::make_shared<MockStage>("Sink", "1.0", 1, 1);
    sink_node.input_node_ids = {"transform_1"};
    dag->add_node(sink_node);
    
    dag->set_output_nodes({"SINK_0"});
    
    // Create renderer
    DAGFieldRenderer renderer(dag);
    
    std::cout << "  Created renderer with " << renderer.get_renderable_nodes().size() << " nodes" << std::endl;
    
    // Test node existence
    assert(renderer.has_node("SOURCE_0"));
    assert(renderer.has_node("transform_1"));
    assert(renderer.has_node("SINK_0"));
    assert(!renderer.has_node("nonexistent"));
    std::cout << "  ✓ Node existence checks passed" << std::endl;
    
    // Test rendering at source
    auto result_source = renderer.render_field_at_node("SOURCE_0", FieldID(0));
    assert(result_source.is_valid);
    assert(result_source.representation != nullptr);
    assert(result_source.representation->has_field(FieldID(0)));
    std::cout << "  ✓ Rendered field at SOURCE_0" << std::endl;
    
    // Test rendering at transform node
    auto result_transform = renderer.render_field_at_node("transform_1", FieldID(1));
    assert(result_transform.is_valid);
    assert(result_transform.representation != nullptr);
    std::cout << "  ✓ Rendered field at transform_1" << std::endl;
    
    // Test caching
    auto result_cached = renderer.render_field_at_node("SOURCE_0", FieldID(0));
    assert(result_cached.from_cache);
    std::cout << "  ✓ Cache working correctly" << std::endl;
    
    std::cout << "[PASS] test_dag_field_renderer_basic\n" << std::endl;
}

void test_dag_change_tracking() {
    std::cout << "=== Testing DAG Change Tracking ===" << std::endl;
    
    // Create initial DAG
    auto dag1 = std::make_shared<DAG>();
    DAGNode node1;
    node1.node_id = "SOURCE_0";
    node1.stage = std::make_shared<MockStage>("Source", "1.0", 0, 1);
    dag1->add_node(node1);
    dag1->set_output_nodes({"SOURCE_0"});
    
    DAGFieldRenderer renderer(dag1);
    auto version1 = renderer.get_dag_version();
    std::cout << "  Initial DAG version: " << version1 << std::endl;
    
    // Render a field
    auto result1 = renderer.render_field_at_node("SOURCE_0", FieldID(0));
    assert(result1.is_valid);
    assert(renderer.cache_size() > 0);
    std::cout << "  ✓ Rendered field, cache size: " << renderer.cache_size() << std::endl;
    
    // Update DAG
    auto dag2 = std::make_shared<DAG>();
    DAGNode node2;
    node2.node_id = "SOURCE_0";
    node2.stage = std::make_shared<MockStage>("Source", "2.0", 0, 1);  // Different version
    dag2->add_node(node2);
    dag2->set_output_nodes({"SOURCE_0"});
    
    renderer.update_dag(dag2);
    auto version2 = renderer.get_dag_version();
    std::cout << "  Updated DAG version: " << version2 << std::endl;
    
    assert(version2 > version1);
    assert(renderer.cache_size() == 0);  // Cache should be cleared
    std::cout << "  ✓ DAG version incremented, cache cleared" << std::endl;
    
    std::cout << "[PASS] test_dag_change_tracking\n" << std::endl;
}

void test_error_handling() {
    std::cout << "=== Testing Error Handling ===" << std::endl;
    
    // Create DAG
    auto dag = std::make_shared<DAG>();
    DAGNode node;
    node.node_id = "SOURCE_0";
    node.stage = std::make_shared<MockStage>("Source", "1.0", 0, 1);
    dag->add_node(node);
    dag->set_output_nodes({"SOURCE_0"});
    
    DAGFieldRenderer renderer(dag);
    
    // Test rendering non-existent node
    auto result_bad_node = renderer.render_field_at_node("nonexistent", FieldID(0));
    assert(!result_bad_node.is_valid);
    assert(!result_bad_node.error_message.empty());
    std::cout << "  ✓ Correctly handled non-existent node" << std::endl;
    std::cout << "    Error: " << result_bad_node.error_message << std::endl;
    
    // Test rendering non-existent field
    auto result_bad_field = renderer.render_field_at_node("SOURCE_0", FieldID(999));
    assert(!result_bad_field.is_valid);
    std::cout << "  ✓ Correctly handled non-existent field" << std::endl;
    std::cout << "    Error: " << result_bad_field.error_message << std::endl;
    
    std::cout << "[PASS] test_error_handling\n" << std::endl;
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << " DAG Field Renderer Tests" << std::endl;
    std::cout << "======================================\n" << std::endl;
    
    try {
        test_dag_field_renderer_basic();
        test_dag_change_tracking();
        test_error_handling();
        
        std::cout << "======================================" << std::endl;
        std::cout << " All tests passed!" << std::endl;
        std::cout << "======================================" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test FAILED with exception: " << e.what() << std::endl;
        return 1;
    }
}
