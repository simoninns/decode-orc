# ARCHITECTURE.md

DRAFT

## ld-decode Next-Generation Tool Chain

## Status

**Normative – implementation reference**

This document defines the high-level software architecture, module boundaries, and dependency rules for the ld-decode next-generation tool chain.

---

## 1. Scope

This document defines:

- Major subsystems and their responsibilities
- Dependency and layering rules
- Ownership and lifetime principles
- Threading and concurrency rules
- Integration boundaries (CLI, GUI, storage)

This document does **not** define:
- User interface layout
- Algorithm implementations
- Storage optimizations

---

## 2. Architectural Overview

The system is structured as a **layered architecture** with a strict dependency direction:

```

+-------------------+
|        GUI        |
+-------------------+
|        CLI        |
+-------------------+
|   Core Execution  |
+-------------------+
|   Storage / IO    |
+-------------------+

```

Higher layers may depend on lower layers, but never the reverse.

---

## 3. Subsystems

### 3.1 Core Execution Layer

**Purpose**
- Owns all domain logic
- Implements the data model, DAG executor, stages, observers, and decisions

**Responsibilities**
- FieldID model
- Artifact identity and provenance
- Video field representations
- Observer framework
- Signal-transforming stages
- DAG validation and execution

**Restrictions**
- MUST NOT depend on Qt UI modules
- MUST NOT assume presence of GUI or CLI
- SHOULD avoid platform-specific APIs

---

### 3.2 Storage / IO Layer

**Purpose**
- Persist and retrieve artifacts and metadata

**Responsibilities**
- SQLite metadata storage
- Artifact serialization
- Cache management

**Restrictions**
- May use Qt SQL or filesystem APIs behind interfaces
- MUST present Qt-free interfaces to Core Execution
- MUST NOT implement domain logic

---

### 3.3 CLI Layer

**Purpose**
- Provide batch and scripting access

**Responsibilities**
- Parse CLI arguments
- Load DAG definitions
- Invoke DAG execution
- Inspect artifacts and observations
- Import/export decision artifacts

**Restrictions**
- MUST NOT reimplement core logic
- MUST NOT bypass DAG execution rules

---

### 3.4 GUI Layer

**Purpose**
- Provide interactive inspection and editing

**Responsibilities**
- DAG visualization and editing
- Field and signal visualization
- Observer output display
- Manual decision editing
- Triggering DAG execution and partial re-execution

**Restrictions**
- MUST NOT modify signal data directly
- MUST NOT implement observers or stages
- MUST treat Core Execution as authoritative

---

## 4. Dependency Rules (Normative)

The following dependencies are permitted:

| From | To |
|----|----|
| GUI | Core Execution |
| CLI | Core Execution |
| Core Execution | Storage |
| CLI | Storage |
| GUI | Storage |

The following dependencies are forbidden:

- Core Execution → GUI
- Core Execution → CLI
- Storage → Core Execution logic
- GUI → CLI

Violations of these rules are architectural errors.

---

## 5. Ownership and Lifetime Model

### 5.1 Artifact Ownership

- Artifacts are immutable
- Artifacts are reference-counted or owned by a central store
- No subsystem may mutate an artifact after creation

### 5.2 Core Object Lifetimes

- Core objects MUST NOT depend on Qt object lifetimes
- Core APIs MUST define ownership explicitly (e.g. shared vs unique)

---

## 6. Threading and Concurrency

### 6.1 Core Execution

- Core execution MAY use internal thread pools
- Stages MUST declare thread-safety
- Parallel execution MUST NOT affect results

### 6.2 GUI Interaction

- Core execution MUST NOT assume a GUI thread
- GUI MUST treat core calls as potentially blocking
- Long-running core tasks SHOULD be executed asynchronously

### 6.3 Observer Execution

- Observers SHOULD be thread-safe
- Observers MUST be deterministic regardless of execution order

---

## 7. Error Handling

### 7.1 Core Errors

- Core errors MUST be represented as structured error objects
- Errors MUST include:
  - Stage or component identifier
  - Artifact or FieldID context where applicable
  - Diagnostic message

### 7.2 UI Presentation

- GUI and CLI are responsible for rendering errors
- Core MUST NOT display UI dialogs or print directly to stdout

---

## 8. Integration Boundaries

### 8.1 Core API Surface

The Core Execution layer MUST expose APIs for:

- DAG loading and validation
- DAG execution control
- Artifact lookup
- Observer query
- Decision import/export

These APIs MUST be stable and versioned.

---

### 8.2 Storage Abstraction

Storage MUST be accessed through interfaces:

- Core MUST NOT depend on SQLite specifics
- Storage backends MAY be replaced without core changes

---

## 9. Build and Linking Model

Recommended structure:

```

core/        -> static or shared library
storage/     -> library
cli/         -> executable
gui/         -> executable

```

Core and Storage libraries MUST be buildable without Qt UI modules.

---

## 10. Cross-Platform Considerations

- Core MUST be portable C++ (standard library only where possible)
- Platform-specific code MUST be isolated
- GUI and CLI handle platform-specific concerns

---

## 11. Architectural Invariants (Non-Negotiable)

- Core logic is independent of UI
- Artifacts are immutable
- DAG is the sole execution model
- Observers are not pipeline stages
- Manual edits are explicit decision artifacts
- Determinism is mandatory

---

## 12. Deferred Topics

The following are intentionally deferred:

- Distributed execution
- Plugin loading mechanisms
- GUI rendering strategies
- Remote execution

These do not block initial implementation.
