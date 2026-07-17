# Observation across the plugin boundary

> **Status:** design proposal / rationale. This document records *why* the
> current observer delivery is a wrinkle and *what* a service-based redesign
> would require. It is not a committed work item — it exists so the decision to
> do (or defer) the refactor is explicit and informed.

## TL;DR

Observer implementations are shipped to plugins as a **static library**
(`orc-sdk-support`): a plugin that needs a measurement `#include`s the observer
header, constructs the C++ class, and links its own private copy of the object
code. Every other host capability that crosses the boundary — the observation
*context*, stage services, colour-preview rendering — is delivered as a
**host-owned implementation reached through an interface or the C service
table**, with a single copy living in the host.

Observers are the odd one out. The fix is to expose observation the same way
the host already exposes `render_colour_preview`: as a **host service**, so the
observer *implementation* lives once in the host and plugins ask for
observations by stable identifier instead of linking observer classes.

This is a self-contained cleanup **within** the existing plugin boundary. It is
**not** the same as giving decode-orc a frozen binary ABI for prebuilt
third-party plugins — see [Scope and non-goals](#scope-and-non-goals).

## How the plugin boundary actually works

It helps to be precise about what is and isn't ABI here, because "ABI" is used
loosely.

The **bootstrap** is a genuine C ABI, versioned and stable:

- `orc_register_stage_plugin(const OrcPluginServices* services, void* context,
  bool (*register_stage)(…), char* error_message)` — the sole exported
  entrypoint, a C signature.
- `OrcPluginServices` — a C struct of function pointers, extended **append-only**
  and guarded by `services_size` so a newer plugin can detect fields an older
  host does not provide (see `<orc/abi/orc_plugin_services.h>`).

Everything the stage *does*, however, crosses the boundary as **C++**:

- The factory returns `std::shared_ptr<DAGStage>` — a C++ object with a vtable
  (`<orc/abi/orc_plugin_abi.h>`, `<orc/stage/stage.h>`).
- The host drives it through virtual calls: `execute(const
  std::vector<ArtifactPtr>&, const std::map<std::string, ParameterValue>&,
  ObservationContext&)`, `version()`, `get_provided_observations()`, …
- `std::shared_ptr`, `std::string`, `std::vector`, `std::map` and the
  `ObservationContext` vtable all pass across the line.

So the decode-orc plugin model is a **same-toolchain C++ ABI with a thin,
version-gated C bootstrap**. Plugins are expected to be compiled against the SDK
with a compatible compiler and standard library and **recompiled when the SDK
changes**. This is a deliberate, reasonable choice for a project that ships its
plugins from the same tree — but it means "the plugin boundary is C++" is the
baseline, not a defect. The observer question lives inside that baseline.

## Two ways observations cross today

There are two distinct mechanisms, and only one of them is clean.

### 1. The observation *context* — host-owned implementation (good)

`ObservationContext` / `IObservationContext`
(`<orc/stage/observation/observation_context_interface.h>`) is an **interface**.
`DAGStage::execute()` receives a reference to a context whose concrete
implementation lives in the **host**. The plugin holds only the vtable declared
by the header; it never links the storage/lookup code. One implementation, one
copy, host-owned. If the host improves the context, plugins pick it up with no
recompile as long as the vtable layout is unchanged.

This is exactly the pattern used by `IStageServices* stage_services` on the
service table, and by `render_colour_preview()` — "equivalent to the
host-internal `render_preview_from_colour_carrier()`", reached as a function
pointer, implemented once in the host.

### 2. Observer *classes* — plugin-linked implementation (the wrinkle)

`WhiteSNRObserver`, `BiphaseObserver`, `ClosedCaptionObserver`, … are concrete
C++ classes. A sink plugin that needs a measurement does:

```cpp
#include <orc/stage/observation/white_snr_observer.h>

WhiteSNRObserver obs;                       // constructed inside the plugin
obs.process_frame(representation, id, ctx); // plugin-side call
```

`process_frame`, the observer vtables, and the base `Observer` vtable are
compiled into the `orc-sdk-support` **static** archive, and **each plugin links
its own private copy** (isolated at load time by `RTLD_LOCAL`). The header sits
in the `orc/stage/` tier — which advertises "a layout change here bumps the host
ABI" — but the *code* is delivered by the `orc/support/` tier, which explicitly
disclaims ABI stability ("changes never require a bump, only a plugin
recompile"). The observer contract is advertised as stage-tier and delivered as
support-tier.

## Why the observer wrinkle is worth removing

It is not a correctness bug today, but it has real costs:

- **N copies in one process.** The observer object code and vtables exist once
  in `orc-core.dylib` and again in every plugin that instantiates an observer.
  Isolation by `RTLD_LOCAL` is what keeps this from being an ODR hazard — it is
  a workaround, not a design.
- **Per-plugin state.** Any static/global or cached state inside an observer is
  duplicated per plugin. There is no single source of truth for "the SNR
  observer" in the running program.
- **Consistency requires re-shipping plugins.** Improve an observer's algorithm
  and every plugin must be rebuilt and redistributed to stay consistent, even
  though nothing in the plugin's own logic changed. With a host-owned observer,
  the host update alone is enough.
- **Mixed stability story.** The observer headers claim stage-tier stability but
  behave as support-tier (recompile-required). A reader cannot tell from the
  tier which promise applies.

Contrast this with the observation *context*, which has none of these problems
precisely because the implementation is host-owned.

## Target design: observation as a host service

Make an observer a thing the plugin *asks the host to run*, not a class the
plugin *links*. This mirrors `render_colour_preview` and `IStageServices`
exactly.

### Interface sketch

Add an observation service, reached from the versioned service table
(append-only, `services_size`-guarded):

```cpp
// New: <orc/stage/observation/observation_service_interface.h>
class IObservationService {
 public:
  virtual ~IObservationService() = default;

  // Discover what the host can measure, by stable string identity.
  virtual std::vector<ObserverInfo> available_observers() const = 0;

  // Run a named observer over a frame, writing results into the
  // caller-provided context. No observer C++ class crosses the boundary.
  virtual bool run_observer(
      std::string_view observer_id,
      const VideoFrameRepresentation& representation,
      FrameID frame_id,
      IObservationContext& context) = 0;
};
```

```cpp
// Appended to OrcPluginServices (guarded by services_size):
IObservationService* observation_service;   // nullptr when unavailable
```

A sink plugin then becomes:

```cpp
auto* svc = plugin::services()->observation_service;
if (svc && svc->run_observer("white_snr", representation, id, ctx)) {
  // read results back out of ctx by (namespace, key), e.g. "white_snr"/"snr_db"
}
```

Key properties:

- **Observer implementations move back into the host** (`orc-core`), single
  copy, invoked on the plugin's behalf. The `orc-sdk-support` archive stops
  shipping observer object code.
- **Plugins reference observers by stable id** (`"white_snr"`) plus the existing
  observation keys (`"white_snr"/"snr_db"`), not by C++ class. Adding an
  observer or a key is an append-only, non-breaking change.
- **Results still flow through the existing observation context**, which is
  already the host-owned interface pattern.
- The concrete `*_observer.h` headers leave the plugin-facing SDK (or are
  reduced to opaque ids/`ObserverInfo`); `sdk_headers.yaml`, the generated
  allowlist, and the include/link gate scripts update to match.

## Migration plan

Phased, with a deprecation window so existing bundled and third-party plugins
keep building throughout.

1. **Add the service (non-breaking).** Introduce `IObservationService`, wire a
   host implementation, and expose it on `OrcPluginServices` as an append-only
   field. The C++ observer classes remain available in parallel. Bump the host
   ABI/services revision for the new field.
2. **Port bundled plugins.** Move `snr_analysis_sink`, `cc_sink`,
   `burst_level_analysis_sink`, `ld_sink`, `daphne_vbi_sink`, and the shared
   `video_sink` off direct observer construction and onto the service.
3. **Deprecate the classes.** Mark the `orc/stage/observation/*_observer.h`
   headers deprecated (shim + warning) for one release so out-of-tree plugins
   can migrate.
4. **Remove the static-lib delivery.** Drop observer sources from
   `orc-sdk-support` back into `orc-core` as host-internal, retire the public
   observer headers from the manifest/allowlist, and delete the deprecation
   shims. Observers are now single-copy and host-owned.

## Scope and non-goals

This refactor makes observation delivery **consistent with the rest of the
boundary**. It does **not**, on its own, deliver a frozen binary ABI in which a
**prebuilt** third-party plugin survives an arbitrary host update without
recompiling.

That larger goal is constrained by the baseline described above: `DAGStage`,
`ObservationContext`, `ArtifactPtr`, and the std containers already cross the
boundary as C++ types under a same-toolchain, recompile-on-change contract.
Achieving true prebuilt-binary compatibility would require reworking the entire
stage interface (factory, `execute`, artifact and context passing) onto a C ABI
— a far larger initiative than observation, and a separate decision. The
observation service is a prerequisite step in that direction (it removes one
class of C++ code from the plugin binary), but it is valuable on its own merits
regardless of whether the project ever pursues the full C ABI.

## References

- `<orc/abi/orc_plugin_services.h>` — the C service table; `render_colour_preview`
  and `IStageServices*` are the precedents this design follows.
- `<orc/abi/orc_plugin_abi.h>`, `<orc/stage/stage.h>` — the C++ stage interface
  that crosses the boundary (`DAGStage`, `OrcStageFactoryFn`).
- `<orc/stage/observation/observation_context_interface.h>` — the host-owned
  interface pattern observers should adopt.
- [Plugin SDK / ABI Review and Improvement Plan](plugin-sdk-abi-review.md) — the
  companion review of the SDK surface, ABI versioning, and distribution story.
- [Plugin SDK Developer Guide](../docs/technical/plugin-sdk.md) — the tier model
  (`abi` / `stage` / `support`) and stability promises.
- [Plugin Architecture](../docs/technical/plugin-architecture.md) — loading,
  isolation, and the registration handshake.
