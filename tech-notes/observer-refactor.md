## Proposed Architecture: Observation Context and Stage-Scoped Observation

There is a fundamental problem with treating observers as either *inputs* or *outputs*. Observations are **side-band metadata** that exist *in addition to* the VFR and cannot be cleanly modelled as part of a universal stage interface.

To resolve this, observers should be redefined as **observation providers** that populate a shared **Observation Context** flowing alongside the VFR through the pipeline.

---

## Key Design Principles

1. **Observations are not VFR**

   * They are metadata derived from the signal, not part of it.
   * They must not be forced into the VFR abstraction.

2. **No universal observation schema exists**

   * Some observations are generic (picture number, frame index).
   * Others are source-specific (e.g. VideoID).
   * The architecture must support both without coupling stages.

3. **Stages decide what observations they need to function**

   * Transforms must explicitly declare required observations.
   * Missing observations must be detectable and fail early.

4. **Sinks decide what observations are emitted**

   * Observation collection and observation output are separate concerns.

5. **No global observers or project-level execution**

   * All ownership is stage-scoped and explicit.

---

## Observation Context

Introduce a pipeline-scoped `ObservationContext` that flows alongside the VFR:

* Lives for the lifetime of pipeline execution
* Passed mutably to all stages
* Stores typed, namespaced observations
* Is not itself an output

Observations are keyed using a `(namespace, name)` pair to avoid forcing a global schema and to allow source-specific extensions.

---

## Observation Providers (Observers)

Observers are redefined as **observation providers**:

* They *measure* properties of the signal
* They write facts into the `ObservationContext`
* They do not format, output, or own results

Observation providers expose:

* Which observation keys they provide
* A method to observe a field/frame and populate the context

They may be instantiated by:

* Source stages (source-specific facts)
* Transform stages (facts required for processing)
* Sink stages (facts intended for output)
* Dedicated analysis stages

This solves the problem where a simple `source → sink` pipeline would otherwise have nowhere to attach observers.

---

## Stage Contracts

Each stage explicitly declares which observations it requires to operate:

* Required observations are declared up front
* Pipeline setup can validate availability
* Execution can fail early if required observations are missing

Stages may also *produce* observations if appropriate.

---

## Sink Responsibility

Sink stages are responsible for **selecting and emitting observations**:

* Sink configuration specifies which observation keys to output
* Sinks read from the `ObservationContext`
* Output format (JSON, sidecar metadata, etc.) is sink-specific

This cleanly separates:

* Observation collection
* Observation dependency
* Observation presentation

---

## Execution Model Summary

* VFR flows through stages as before
* Observation Context flows alongside it
* Observation providers populate the context
* Stages consume observations via explicit contracts
* Sinks decide what metadata is emitted

---

## Benefits

* Solves the “observers as inputs vs outputs” conflict
* Supports simple and complex pipelines equally
* Avoids a false “standard observation interface”
* Eliminates hidden dependencies and global state
* Enables modular, testable, stage-scoped observers

---

## Conclusion

Observations should be treated as **side-band facts**, not stage outputs. Introducing a shared Observation Context with explicit stage contracts allows observers to remain modular, scoped, and configurable while supporting both transform requirements and user-visible metadata output.

Below is **drop-in text** you can append to the previous Issue #65 proposal. It is written to be architectural, precise, and implementation-oriented, without introducing new concepts beyond what has already been discussed.

---

## Observer Configuration

Observers are configured **by the stage that owns them**, using stage-local configuration blocks. This avoids global configuration, supports multiple differently-configured instances of the same observer type, and reflects the fact that many observations (e.g. VITC line placement) have no universal standard.

### Observer Configuration Schema

Each observer type exposes a **configuration schema** that describes:

* Required configuration parameters
* Optional parameters with defaults
* Parameter types and valid ranges

This schema is used to validate user configuration during pipeline setup, before execution begins.

Configuration defaults belong to the observer itself and represent domain knowledge, not pipeline-level policy.

---

### Stage-Scoped Configuration

Stages that instantiate observers provide configuration inline as part of their own configuration. For example:

* A sink stage may configure observers for metadata extraction
* A transform stage may configure observers required for correct operation
* A source stage may configure observers for source-specific measurements

Configuration is **per observer instance**, not per observer type, allowing multiple instances of the same observer to coexist with different parameters.

---

### Multiple Instances

Because configuration is instance-scoped, a stage may instantiate multiple observers of the same type with different configurations. This is essential for real-world analogue decoding scenarios where multiple candidate signals or line positions must be evaluated.

Each instance produces observations that are uniquely identified within the Observation Context.

---

### Validation and Failure Modes

Observer configuration is validated during pipeline construction:

* Required parameters must be present
* Values must conform to the observer’s schema
* Stages declare required observations and validation ensures those observations can be produced

If validation fails, the pipeline fails to construct, ensuring that decoding never starts in an invalid or ambiguous configuration state.

---

### Separation from Output Configuration

Observer configuration controls **how observations are collected**, not how they are emitted.

Sink stages separately configure which observations are output as metadata. This ensures a clear separation between:

* Measurement (observers)
* Dependency (stage requirements)
* Presentation (sink output)

---

### Summary

Observer configuration is:

* Stage-scoped
* Schema-validated
* Instance-specific
* Free of global defaults
* Explicit and deterministic

This approach supports both simple and advanced pipelines while avoiding hidden dependencies, implicit standards, and project-wide configuration coupling.

