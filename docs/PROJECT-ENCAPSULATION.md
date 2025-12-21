# Project Encapsulation Architecture

**Date:** December 21, 2025  
**Status:** ENFORCED - DO NOT BREAK

## Overview

The `Project` class uses **strict encapsulation** to enforce clear separation between business logic (core) and UI/CLI layers. This architecture prevents business logic from leaking into the GUI and ensures all state modifications go through a single, controlled path.

## Architecture Rules

### 1. ALL Project Fields Are Private

```cpp
class Project {
private:
    std::string name_;
    std::string description_;
    std::string version_;
    std::vector<ProjectDAGNode> nodes_;
    std::vector<ProjectDAGEdge> edges_;
    mutable bool is_modified_;
};
```

**These fields MUST remain private.** Never make them public or protected.

### 2. Read Access: Public Const Getters

External code (GUI, CLI, analysis) can **read** Project state via public const getters:

```cpp
const std::string& get_name() const;
const std::string& get_description() const;
const std::string& get_version() const;
const std::vector<ProjectDAGNode>& get_nodes() const;
const std::vector<ProjectDAGEdge>& get_edges() const;
bool has_unsaved_changes() const;
```

These return **const references** - no modification allowed.

### 3. Write Access: ONLY via project_io Functions

All Project modifications **MUST** go through `project_io` namespace functions:

```cpp
namespace project_io {
    Project load_project(const std::string& filename);
    void save_project(const Project& project, const std::string& filename);
    Project create_empty_project(const std::string& project_name);
    
    std::string add_node(Project& project, ...);
    void remove_node(Project& project, const std::string& node_id);
    void change_node_type(Project& project, ...);
    void set_node_parameters(Project& project, ...);
    void set_node_position(Project& project, ...);
    void set_node_label(Project& project, ...);
    
    void add_edge(Project& project, ...);
    void remove_edge(Project& project, ...);
    
    bool trigger_node(Project& project, ...);
    std::string find_source_file_for_node(const Project& project, ...);
}
```

### 4. Friend Declarations

`project_io` functions access private fields via **friend declarations** in `project.h`:

```cpp
class Project {
private:
    // ... fields ...
    
    friend Project project_io::load_project(...);
    friend void project_io::save_project(...);
    friend std::string project_io::add_node(...);
    // ... etc for all project_io functions
};
```

## What This Enforces

✅ **Single Point of Modification**: Only `project_io` functions can change Project state  
✅ **Consistent Tracking**: All modifications set `is_modified_ = true`  
✅ **Clear Separation**: Business logic stays in core, UI stays in gui  
✅ **Type Safety**: Const references prevent accidental modification  
✅ **Compile-Time Enforcement**: Private fields = compilation error if violated  

## Usage Examples

### ✅ CORRECT: Read via getters

```cpp
// GUI/CLI code
const auto& nodes = project.get_nodes();
for (const auto& node : nodes) {
    std::cout << node.node_id << "\n";
}
```

### ✅ CORRECT: Modify via project_io

```cpp
// GUI/CLI code
project_io::add_node(project, "TBCSource", 100, 100);
project_io::set_node_parameters(project, node_id, new_params);
```

### ❌ WRONG: Direct field access

```cpp
// This will NOT compile - fields are private
project.name_ = "New Name";           // ERROR!
project.nodes_.push_back(new_node);   // ERROR!
```

### ❌ WRONG: Non-const getter

```cpp
// DO NOT add this to Project class
std::vector<ProjectDAGNode>& get_nodes(); // NO! Returns mutable reference
```

## Adding New Functionality

When you need to add new Project modification capability:

1. **Add function to project_io namespace** in `orc/core/project.cpp`:
```cpp
namespace project_io {
    void set_project_name(Project& project, const std::string& name) {
        project.name_ = name;
        project.is_modified_ = true;
    }
}
```

2. **Forward-declare in project.h** (before Project class):
```cpp
namespace project_io {
    void set_project_name(Project& project, const std::string& name);
}
```

3. **Add friend declaration** in Project class:
```cpp
class Project {
private:
    friend void project_io::set_project_name(Project& project, const std::string& name);
};
```

4. **Use from GUI/CLI**:
```cpp
project_io::set_project_name(project, new_name);
```

## Files With Architecture Notes

The following files contain inline documentation about this architecture:

- `/orc/core/include/project.h` - Class definition with architecture rules
- `/orc/core/project.cpp` - Implementation with modification guidelines
- `/orc/core/project_to_dag.cpp` - Read-only access example

## Verification

To verify encapsulation is maintained:

```bash
# Should find ZERO matches - all fields are private with _ suffix
grep -r "project\\.name[^_]" orc/gui/ orc/cli/
grep -r "project\\.nodes[^_]" orc/gui/ orc/cli/
grep -r "project\\.edges[^_]" orc/gui/ orc/cli/
```

If any matches are found, the encapsulation has been broken.

## Why This Matters

This architecture prevents:
- ❌ Business logic leaking into GUI code
- ❌ Inconsistent state (modifications without tracking)
- ❌ Direct field access bypassing validation
- ❌ Difficult-to-trace bugs from scattered modifications
- ❌ Violation of single responsibility principle

And enables:
- ✅ Clear ownership of Project state (core layer)
- ✅ Testable business logic (isolated from UI)
- ✅ Consistent modification tracking
- ✅ Easy refactoring (single modification point)
- ✅ Compile-time guarantees via const-correctness

## DO NOT BREAK THIS

**This is a fundamental architectural decision.**

If you're tempted to make a field public "just for convenience" - **DON'T**.  
Add a proper `project_io` function instead.

The slight inconvenience of adding a new function is **vastly outweighed** by the architectural benefits and bug prevention this provides.
