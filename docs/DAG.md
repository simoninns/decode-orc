# DAG.md

DRAFT

## ld-decode Next-Generation Tool Chain

## Status

**Normative – implementation reference**

This document defines how processing DAGs are represented, validated, executed, cached, and partially re-executed in the ld-decode next-generation tool chain.

---

## 1. Scope

This document defines:

- DAG representation (nodes, edges, parameters)
- Stage and artifact semantics
- Cache keys and invalidation rules
- Execution ordering and partial execution
- Failure handling

This document does **not** define:
- Algorithm details for individual stages
- UI behavior beyond execution requirements

---

## 2. Terminology

### 2.1 Directed Acyclic Graph (DAG)

A **Directed Acyclic Graph** is a graph of processing stages where:

- Nodes represent signal-transforming stages
- Directed edges represent artifact dependencies
- Cycles are forbidden

---

### 2.2 Stage

A **Stage** is a signal-transforming processing unit.

Properties:
- Consumes one or more input artifacts
- Produces one or more output artifacts
- Is deterministic for given inputs and parameters
- Appears as a node in the DAG

Observers are explicitly **not** stages.

---

### 2.3 Artifact

An **Artifact** is an immutable, versioned data object.

Artifacts include:
- Video field representations
- Decision artifacts
- Derived signal representations (e.g. corrected or stacked fields)
- Export outputs

Artifacts are identified by an `ArtifactID` and tracked via provenance.

---

### 2.4 Representation

A **Video Field Representation** is a specific artifact providing access to video field samples and descriptors (see `DATA_MODEL.md`).

---

## 3. DAG Representation

### 3.1 DAG Document

A DAG is defined by a serializable document (YAML or JSON).

The format MUST support:
- Unique node identifiers
- Stage type names
- Optional stage version constraints
- Input bindings
- Parameter sets
- Optional UI metadata (labels, grouping)

---

### 3.2 Node Definition

Each DAG node MUST define:

- `id`  
  Unique identifier within the DAG.

- `type`  
  Stage type name (e.g. `DropoutCorrect`, `StackFields`).

- `version` (optional)  
  Exact version, semver range, or implementation hash.

- `inputs`  
  Named input ports bound to upstream node outputs or external artifacts.

- `params`  
  Typed, deterministic stage parameters.

- `outputs`  
  Named output ports as declared by the stage type.

---

### 3.3 Example DAG (illustrative)

```yaml
nodes:
  - id: import
    type: ImportTbc
    params:
      tbc_path: "capture.tbc"
      metadata_path: "capture.sqlite"

  - id: correct
    type: DropoutCorrect
    inputs:
      fields: import.fields
      decisions: external.dropout_decisions
    params:
      method: "interpolate"
      max_span: 24

  - id: stack
    type: StackFields
    inputs:
      fields: correct.fields
    params:
      window: 3

  - id: export
    type: Export
    inputs:
      fields: stack.fields
    params:
      format: "tbc"
      output_path: "out.tbc"
````

---

## 4. Stage Contract

### 4.1 Stage Declaration

Each stage type MUST declare:

* Stage type name
* Implementation version identifier
* Input ports:

  * Port name
  * Required artifact type
* Output ports:

  * Port name
  * Artifact type
* Parameter schema:

  * Names
  * Types
  * Defaults
  * Constraints
* Determinism guarantee

---

### 4.2 Execution Contract

When executed, a stage MUST:

1. Read all inputs via artifact handles
2. Produce outputs atomically
3. Record provenance:

   * Input artifact IDs
   * Canonical parameters
   * Stage type and version
4. Never mutate input artifacts
5. Never depend on undeclared external state

---

## 5. Artifact Binding and Ports

### 5.1 Port Binding Syntax

An input binding references:

* An upstream node output
  `nodeId.outputPort`
* An external artifact
  `external.artifactName`

Bindings MUST be resolvable before execution.

---

### 5.2 Artifact Types

The executor MUST support, at minimum:

* Video field representation artifacts
* Decision artifacts
* Optional: PCM, EFM, disc map, export artifacts

Artifact types are defined in `DATA_MODEL.md`.

---

## 6. Caching and Invalidation

### 6.1 Cache Key (Normative)

Each node execution produces a **Run Key** derived from:

1. Stage type name
2. Stage implementation version
3. Canonicalized parameters
4. IDs of all input artifacts
5. Declared deterministic environment inputs (if any)

If an artifact exists for the same Run Key, execution MUST be skipped and the cached artifact reused.

---

### 6.2 Parameter Canonicalization

Parameters MUST be canonicalized deterministically:

* Stable key ordering
* Explicit defaults applied
* Stable numeric formatting
* No locale-dependent behavior

---

### 6.3 Invalidation Rules

A node is invalidated if and only if its Run Key changes.

Downstream nodes are invalidated transitively through dependencies.

---

### 6.4 Environment Dependencies

Stages MUST NOT read mutable environment state (time, locale, RNG, filesystem probes).

If unavoidable, such dependencies MUST:

* Be declared explicitly
* Be included in the cache key

---

## 7. Execution Semantics

### 7.1 Validation

Before execution, the DAG executor MUST validate:

* Unique node IDs
* All bindings resolve
* Artifact types are compatible
* DAG is acyclic
* Required external artifacts exist

Execution MUST NOT begin if validation fails.

---

### 7.2 Scheduling

Execution order is determined by topological sorting.

Parallel execution is permitted when:

* Dependencies are satisfied
* Stages declare themselves parallel-safe
* Resource constraints allow

Parallel execution MUST NOT affect results.

---

### 7.3 Atomicity

Stages MUST write outputs to temporary locations and commit atomically.

Partial artifacts MUST NOT be visible after failure.

---

### 7.4 Provenance Recording

For each produced artifact, provenance MUST include:

* Stage type and version
* Input artifact IDs
* Canonical parameters
* Optional execution metadata (warnings, timing)

---

## 8. Partial Execution

### 8.1 Execution Targets

The executor MUST support:

* Execute entire DAG
* Execute up to a target node
* Execute from a given node onward
* Execute a selected subset with dependency closure

---

### 8.2 Preview Execution

Stages MAY support preview modes such as:

* FieldID range restriction
* Limited field count
* Reduced computation fidelity

Preview constraints MUST be included in the cache key if they affect outputs.

---

## 9. Observer Integration (Non-DAG)

Observers do not appear as DAG nodes.

### 9.1 Observer Inputs

Observers consume:

* A video field representation artifact
* Optional hint metadata

---

### 9.2 Observer Caching

Observer results SHOULD be cached using:

* Video representation ArtifactID
* Observer type and version
* Observer parameters
* Hint source identifiers (if applicable)

Observer caches MUST be invalidated when the representation ArtifactID changes.

---

### 9.3 Stage Use of Observations

Stages MAY consume observer outputs implicitly.

Observer outputs MUST NOT:

* Affect DAG structure
* Be treated as decision artifacts
* Be mutated by stages

---

## 10. Failure Semantics

* Stage failure aborts downstream execution
* Previously cached artifacts remain valid
* Partial outputs MUST NOT be committed
* Failures MUST be reported with:

  * Node ID
  * Stage type
  * Diagnostic information

---

## 11. Invariants (Non-Negotiable)

* DAGs are static and acyclic
* Only signal-transforming stages appear in the DAG
* Artifacts are immutable
* Caching is deterministic
* Observers are not stages
* Decisions are explicit artifacts

---

## 12. Deferred Topics

The following are explicitly deferred:

* Fine-grained scheduling heuristics
* Distributed execution
* Persistent cache eviction policy
* Observer parallelism strategies

These do not block initial implementation.

---

