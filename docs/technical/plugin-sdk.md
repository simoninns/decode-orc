# Plugin SDK Developer Guide

This guide covers everything needed to create, build, test, and distribute a
third-party Decode-Orc stage plugin using the public Plugin SDK. No changes to
the Decode-Orc source repository are required.

## Quick Start

First install the SDK package from a decode-orc checkout (see
[Obtaining the SDK](#obtaining-the-sdk) below), then build a plugin against
it. The smallest complete, in-repo example is the CI fixture plugin at
`orc-tests/fixtures/external-stage-plugin/` — three files (CMakeLists.txt,
plugin.cpp, one stage header) that configure, build, and load exactly as an
external plugin:

```bash
cmake -S my-orc-plugin -B my-orc-plugin/build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/path/to/sdk-prefix
cmake --build my-orc-plugin/build -j
```

Alternatively, clone the official skeleton template for a scaffold with unit
tests and CI workflows (note: the skeleton repository may lag the SDK — the
in-repo fixture above is always in sync with the installed package):

```bash
git clone https://github.com/simoninns/orc-plugin_skeleton my-orc-plugin
```

## SDK Headers

All plugin code must include only the public SDK umbrella header:

```cpp
#include <orc/plugin/orc_plugin_sdk.h>
```

This pulls in all stable contracts:

| Header | Provides |
|--------|----------|
| `<orc/plugin/orc_plugin_abi.h>` | `StagePluginDescriptor`, `OrcStageFactoryFn`, `ORC_STAGE_PLUGIN_EXPORT`, `kStagePluginHostAbiVersion`, `kStagePluginApiVersion`, `ORC_SDK_TOOLCHAIN_TAG`, entrypoint symbol names |
| `<orc/plugin/orc_stage_api.h>` | `ParameterizedStage`, `TriggerableStage`, `NodeTypeInfo`, `ParameterValue`, `ParameterDescriptor`, `VideoSystem`, `SourceType` |
| `<orc/plugin/orc_stage_runtime.h>` | Stage runtime include surface for stage implementations (observation context, triggerable stage) |
| `<orc/plugin/orc_stage_preview.h>` | Preview capability and carrier contracts for preview-capable stages |
| `<orc/plugin/orc_stage_services.h>` | `IStageServices`, `IFileWriterUint8`, `IFileWriterUint16`, `IFileWriterInt16` — host-provided buffered file-output factories for sink stages |
| `<orc/plugin/orc_stage_tooling.h>` | `StageToolDescriptor`, `StageToolProvider`, `AnalysisToolDescriptor`, `AnalysisToolProvider`, `ORC_STAGE_INSTRUCTIONS_MD` — optional tool contracts and stage self-documentation |
| `<orc/plugin/orc_plugin_services.h>` | `OrcPluginServices` host service table, `OrcPluginLogLevel`, `orc::plugin::set_services()`, `orc::plugin::get_stage_services()` |
| `<orc/plugin/orc_plugin_services_helpers.h>` | `ORC_PLUGIN_LOG_*` logging macros |
| `<orc/plugin/orc_plugin_registration.h>` | `ORC_STAGE_PLUGIN_DESCRIPTOR`, `ORC_DEFINE_STAGE_PLUGIN` — descriptor and entrypoint boilerplate helpers |

In addition to the `<orc/plugin/...>` family, the SDK ships the stage
contract tree `<orc/stage/...>`. Stage implementation code may include these
headers directly; the umbrella headers above pull in the core subset
automatically. The complete allowlisted `<orc/stage/...>` set is:

| Group | Headers |
|-------|---------|
| Stage model | `stage.h`, `triggerable_stage.h`, `stage_parameter.h`, `parameter_types.h`, `node_type.h`, `node_id.h`, `artifact.h`, `analysis_sink_results.h` |
| Frame / signal model | `audio_track.h`, `video_frame_representation.h`, `video_metadata_types.h`, `frame_descriptor.h`, `frame_id.h`, `field_id.h`, `frame_line_util.h`, `common_types.h`, `cvbs_signal_constants.h`, `orc_source_parameters.h`, `dropout_run.h`, `dropout_util.h`, `dropout_decision.h` |
| Observation model | `observation_context.h`, `observation_context_interface.h`, `observation_schema.h`, `observers/observer.h`, `observers/biphase_observer.h`, `observers/black_psnr_observer.h`, `observers/burst_level_observer.h`, `observers/closed_caption_observer.h`, `observers/white_snr_observer.h` |
| Preview contract | `stage_preview_capability.h`, `stage_custom_preview_renderer.h`, `colour_preview_provider.h`, `colour_preview_conversion.h`, `preview_helpers.h`, `preview_stage_types.h`, `orc_preview_types.h`, `orc_preview_carriers.h`, `orc_rendering.h`, `orc_vectorscope.h` |
| Utilities | `logging.h`, `lru_cache.h`, `file_io_interface.h`, `eia608_decoder.h`, `error_types.h` |

This list, together with the `<orc/plugin/...>` family above, is the
**enforced include allowlist**: the gate script
(`cmake/check_plugin_private_includes.sh`) fails the build on any plugin
include outside it, other than plugin-local headers, standard-library and
platform headers, and permitted third-party headers (`fmt`/`spdlog`
unconditionally; other libraries when declared by the plugin's own CMake
target).

### Design Notes on the Allowlist

A few allowlist entries carry rationale worth knowing:

- **Logging surface.** In-tree plugins use the `ORC_LOG_*` macro family from
  `<orc/stage/logging.h>`, backed by the host's spdlog logger and linked from
  orc-core like every other contract symbol. Out-of-tree plugins should
  prefer the services-table macros (`ORC_PLUGIN_LOG_*` from
  `<orc/plugin/orc_plugin_services_helpers.h>`) to avoid a direct spdlog
  dependency. The application logging header `orc/common/include/logging.h`
  (`get_app_logger()`) is unrelated and is the GUI/CLI surface only; the
  contract header lives at `<orc/stage/logging.h>` precisely so the two can
  never be confused via include-path order.
- **Observer headers are provisional.** The `observers/*.h` entries exist
  because the analysis sinks instantiate concrete host observers to
  (re)compute observations at trigger time. The cleaner long-term design is a
  host-side observation service exposed through `IStageServices`; if that is
  added, these headers return to host-only status. Do not grow new
  dependencies on them.
- **`lru_cache.h`** is a self-contained generic container with no host
  coupling, allowlisted because several plugins legitimately use it for frame
  caching — preferable to each plugin vendoring a copy.

### Stability Guarantees

Headers in the public SDK carry an explicit stability commitment:
- Changes to ABI-level contracts require a bump to `kStagePluginHostAbiVersion`.
- Changes to stage API contracts require a bump to `kStagePluginApiVersion`.
- Incompatible plugins are refused by the host with a diagnostic message;
  they are never silently mis-loaded.

## What Plugins Must Not Include

The SDK boundary is an **allowlist**: anything not listed in the SDK Headers
section above (or covered by the plugin-local / standard-library /
third-party allowances) is private and will change without notice. Typical
private surfaces plugins must never reach into:

| Forbidden header path | Reason |
|-----------------------|--------|
| `orc/core/include/*.h` | Internal host implementation headers (e.g. `dag_executor.h`, `preview_renderer.h`, `stage_registry.h`, `buffered_file_io.h`) |
| `orc/core/analysis/*.h`, `orc/core/observers/*.h` | Host analysis/observer internals (the observer headers plugins may use live under `<orc/stage/observers/...>`) |
| `orc/presenters/*.h` | Presenter layer — GUI/CLI internal |
| `orc/gui/*.h` | GUI internal |
| `orc/cli/*.h` | CLI internal |

The boundary is enforced twice:

1. **At compile time** — the `orc::plugin-sdk` target exposes only the SDK
   include tree (plus spdlog/fmt). Plugins link `orc-core` for symbols only
   (`$<LINK_ONLY:...>`), so a plugin translation unit that includes a private
   host header fails to compile.
2. **By the allowlist scan** — `check_plugin_private_includes.sh` (a hard CI
   gate, `ctest -L sdk`) fails on any include outside the allowlist even if
   it would compile.

Do not link directly against `orc-core`, `orc-presenters`, `orc-gui`, or
`orc-cli`. Link only against `orc::plugin-sdk`. Third-party libraries a
plugin needs (FFmpeg, SQLite, FFTW, ...) must be declared by the plugin's
own CMake target — they are no longer inherited from the host.

If a capability you need is missing from the SDK, open an issue in the
decode-orc repository. Missing capability is never a reason to include private
headers or link private internals.

## CMake Integration

### Obtaining the SDK

The SDK is installed as part of a decode-orc install tree. From a decode-orc
checkout:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cmake --install build --prefix /path/to/sdk-prefix
```

The prefix then contains the CMake package
(`lib/cmake/decode-orc-plugin-sdk/`), the public SDK headers
(`include/decode-orc-plugin-sdk/`), the host libraries plugins link against,
and the `orc-cli`/`orc-gui` binaries used to load and test your plugin.

### Out-of-tree plugin project

```cmake
cmake_minimum_required(VERSION 3.20)
project(my-orc-stage VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(decode-orc-plugin-sdk REQUIRED)

orc_add_stage_plugin(
    my-orc-stage-my-filter
    OUTPUT_NAME    orc-plugin_my-filter_my-stage
    PLUGIN_VERSION "${PROJECT_VERSION}"
    SOURCES        plugin.cpp my_filter_stage.cpp
)
```

Configure with the SDK prefix on `CMAKE_PREFIX_PATH`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=/path/to/sdk-prefix
cmake --build build -j
```

The built plugin lands in `<build-dir>/lib/orc-stage-plugins/`. Load it into
the host with the plugin search path environment variable:

```bash
ORC_STAGE_PLUGIN_PATHS=<build-dir>/lib/orc-stage-plugins orc-cli plugins list
```

The installed `decode-orc-plugin-sdk` CMake package provides:
- `orc::plugin-sdk` — imported INTERFACE target with the SDK include paths,
  spdlog/fmt, and the host link targets (`orc::orc-core`, `orc::orc-common`)
  as link-only dependencies — never link those two directly
- `orc_add_stage_plugin()` — plugin target helper macro
- The public SDK headers (`<orc/plugin/...>`, `<orc/stage/...>`)
- `check_plugin_private_includes.sh` / `check_plugin_private_links.sh` — SDK enforcement gate scripts

This exact flow is validated in decode-orc CI on every push: the fixture
plugin `orc-tests/fixtures/external-stage-plugin/` is built against a
freshly installed prefix and loaded into the installed `orc-cli`.

### Validating SDK-only compliance locally

To run the same enforcement gates that execute in Decode-Orc CI/CD, use the
scripts bundled with the installed SDK package (located at
`<install-prefix>/lib/cmake/decode-orc-plugin-sdk/`):

```bash
# Check every include in your plugin tree against the SDK allowlist
bash <sdk-install>/check_plugin_private_includes.sh /path/to/your-plugin-repo

# Check for forbidden private-link dependencies in your plugin target
bash <sdk-install>/check_plugin_private_links.sh /path/to/your-plugin-repo
```

When pointed at a tree that is not a decode-orc checkout, both scripts run in
standalone mode: the whole tree is scanned as a single plugin (includes may
resolve anywhere within it). Both scripts exit with code 1 and print a
diagnostic on violations. Run them before opening a pull request or tagging a
release.

### Output naming

Follow the artifact naming convention so the host can validate and cache
plugin binaries correctly:

```
orc-plugin_<stage-name>_<platform>.<ext>
```

## Implementing a Stage

### Minimal plugin structure

The registration helpers in `<orc/plugin/orc_plugin_registration.h>` (pulled
in by the umbrella header) expand the entire plugin boilerplate — descriptor
version/toolchain fields, both entrypoints, service-table storage, and stage
registration — from two statements:

```cpp
#include <orc/plugin/orc_plugin_sdk.h>

#include "my_stage.h"  // Your DAGStage subclass

namespace {

// The ABI version, API version, and toolchain tag are filled in from the SDK
// the plugin is compiled against. All pointer fields point to static storage.
constexpr orc::StagePluginDescriptor kDescriptor =
    ORC_STAGE_PLUGIN_DESCRIPTOR("com.example.stage.my-filter",  // plugin_id
                                "1.0.0",  // plugin_version
                                "MIT",    // license_spdx
                                false);   // is_core_plugin

}  // namespace

// Expands to both required entrypoints. MyStage is registered under the
// stage name reported by its own get_node_type_info().stage_name — the stage
// class is the single source of truth for registration metadata. List
// additional stage types as further arguments to register several stages
// from one plugin.
ORC_DEFINE_STAGE_PLUGIN(kDescriptor, MyStage)
```

Stage types passed to `ORC_DEFINE_STAGE_PLUGIN` must be either
default-constructible or constructible from `orc::IStageServices*`; in the
latter case the host's consolidated stage services are injected automatically
when the stage is created.

The same pattern is used by every bundled stage plugin — see
`orc/plugins/stages/mask_line/plugin.cpp` (single stage) or
`orc/plugins/stages/cvbs_source/plugin.cpp` (multiple stages) in the
Decode-Orc repository for complete real-world examples.

### Raw entrypoints (custom registration logic)

Plugins that need custom registration logic — stages with other constructor
signatures, conditional registration, dynamic stage sets — can write the two
exported entrypoints by hand instead:

```cpp
#include <orc/plugin/orc_plugin_sdk.h>

#include "my_stage.h"

namespace {

// Factory the host calls to create stage instances on demand.
// Must match orc::OrcStageFactoryFn: std::shared_ptr<DAGStage> (*)().
orc::DAGStagePtr create_my_stage() { return std::make_shared<MyStage>(); }

// Plugin descriptor. Fields are positional and must follow the declaration
// order in <orc/plugin/orc_plugin_abi.h>. All pointer fields must point to
// static storage.
constexpr orc::StagePluginDescriptor kDescriptor{
    "com.example.stage.my-filter",    // plugin_id
    "1.0.0",                          // plugin_version
    orc::kStagePluginHostAbiVersion,  // host_abi_version
    orc::kStagePluginApiVersion,      // plugin_api_version
    "MIT",                            // license_spdx
    false,                            // is_core_plugin
    ORC_SDK_TOOLCHAIN_TAG,            // toolchain_tag
};

}  // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor*
orc_get_stage_plugin_descriptor() {
  return &kDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
    const orc::OrcPluginServices* services, void* context,
    bool (*register_stage)(void* context, const char* stage_name,
                           orc::OrcStageFactoryFn factory),
    const char** error_message) {
  // Store the host service table before registering any stage; the pointer
  // remains valid for the lifetime of the loaded plugin.
  orc::plugin::set_services(services);

  if (!register_stage) {
    if (error_message) {
      *error_message = "Missing stage registration callback";
    }
    return false;
  }

  if (!register_stage(context, "my_filter", &create_my_stage)) {
    if (error_message) {
      *error_message = "Host rejected stage registration";
    }
    return false;
  }

  return true;
}
```

### Stage interfaces

Implement one or more of these interfaces depending on stage behaviour:

| Interface | Use when |
|-----------|----------|
| `DAGStage` | Base interface — all stages |
| `ParameterizedStage` | Stage exposes user-configurable parameters |
| `TriggerableStage` | Stage responds to external trigger events |

### Receiving data

Override `DAGStage::execute()` to receive `ArtifactPtr` inputs from upstream
nodes. Return a vector of `ArtifactPtr` outputs to pass downstream.

### Transform stages: VideoFrameRepresentationWrapper contract

Transform stages that expose video output extend
`VideoFrameRepresentationWrapper` (in
`<orc/stage/video_frame_representation.h>`). The wrapper's public read API
**always describes the stage's output**: downstream stages, observers, and
analysis sinks can never reach unprocessed upstream data through any
accessor, and stages themselves may only read from the wrapped input
(`source_`), never from anything further upstream.

The accessors fall into two groups:

- **Pass-through primitives** (forwarded to the wrapped input): navigation
  (`frame_range`, `frame_count`, `has_frame`, `get_frame_descriptor`),
  `get_frame`, `has_separate_channels`, `get_frame_luma`,
  `get_frame_chroma`, `get_dropout_hints`, `get_video_parameters`, and the
  audio / EFM / AC3 accessors. A stage whose output differs from its input
  for one of these **must** override it. A stage that remaps frame IDs must
  override every per-frame accessor in this group so IDs are translated —
  for audio that means the frame-locked per-track accessors
  (`get_audio_sample_count` / `get_audio_samples`); the free-running stream
  accessors (`get_audio_stream_pair_count` / `get_audio_stream_samples`)
  carry no frame IDs and forward untouched.
- **Derived accessors** (`get_line`, `get_line_samples`, `get_frame_copy`,
  `get_line_luma`, `get_line_chroma`): implemented by the wrapper in terms
  of the object's own virtual primitives, never forwarded. Overriding the
  frame-level primitives is therefore sufficient for correctness on every
  read path; overriding a derived accessor is purely an optimisation (for
  example, a stage that does not modify sample data may forward line reads
  to the wrapped input to preserve a source's seek-one-line-from-disk fast
  path).

This contract is verified by
`orc-tests/core/unit/contracts/video_frame_representation_wrapper_contract_test.cpp`.

### Parameters

`ParameterizedStage` exposes three methods:

- `get_parameter_descriptors(VideoSystem, SourceType)` — returns the list of
  `ParameterDescriptor` records this stage supports, filtered by the project's
  video format and source type (a no-argument convenience overload passes
  `Unknown` for both).
- `get_parameters()` — returns the current values as a
  `std::map<std::string, ParameterValue>`.
- `set_parameters(const std::map<std::string, ParameterValue>&)` — applies new
  values; returns `true` if all parameters were valid and set successfully.

`ParameterValue` is a `std::variant` holding one of: `int32_t`, `uint32_t`,
`double`, `bool`, or `std::string`. The corresponding `ParameterType` enum
additionally distinguishes `FILE_PATH` (a string for which the GUI shows a
file browser).

### Configuration status

Stages can report their readiness to the host UI via `set_configuration_status()`,
which accepts a value from the `orc::ConfigurationStatus` enum:

| Value | Meaning |
|-------|---------|
| `ConfigurationStatus::Green` | Fully configured or requires no parameters |
| `ConfigurationStatus::Yellow` | Partially configured; some parameters are missing or empty |
| `ConfigurationStatus::Red` | Unconfigured; critical required parameters are absent |

The host renders a small coloured dot on the stage node in the pipeline graph to
give the user at-a-glance feedback.

**When to call `set_configuration_status()`:**

- In the constructor — set `Yellow` if the stage requires at least one parameter
  that has no sensible default, `Green` if all parameters have working defaults.
- At the end of `set_parameters()` — re-evaluate and call again based on the
  values that were just applied.

```cpp
MyStage::MyStage() {
    set_configuration_status(orc::ConfigurationStatus::Yellow);
}

bool MyStage::set_parameters(
    const std::map<std::string, orc::ParameterValue>& params) {
    auto it = params.find("output_path");
    const bool ready =
        it != params.end() &&
        std::holds_alternative<std::string>(it->second) &&
        !std::get<std::string>(it->second).empty();

    set_configuration_status(ready ? orc::ConfigurationStatus::Green
                                   : orc::ConfigurationStatus::Yellow);
    return true;
}
```

Read the current value with `get_configuration_status()` if needed. The default
(when `set_configuration_status()` is never called) is `Green`.

### Host services

The host passes an `OrcPluginServices` table as the first argument to
`orc_register_stage_plugin()`. Store it with `orc::plugin::set_services()`
before registering any stage. The table provides:

- `log` — pre-formatted message logging routed to the host logger. Use the
  `ORC_PLUGIN_LOG_TRACE` / `DEBUG` / `INFO` / `WARN` / `ERROR` / `CRITICAL`
  macros (from `<orc/plugin/orc_plugin_services_helpers.h>`) rather than
  calling the function pointer directly. Logging does **not** go through
  `IStageServices`.
- `render_colour_preview` — converts a decoded `ColourFrameCarrier` to a
  display-ready `PreviewImage` for preview-capable stages.
- `stage_services` — optional pointer to the consolidated `IStageServices`
  interface; may be `nullptr` when the capability is unavailable. Retrieve it
  with `orc::plugin::get_stage_services()` (no arguments), which returns
  `nullptr` if the services table is absent or predates the field.

`IStageServices` currently exposes exactly three factory methods, used by sink
stages for buffered file output:

- `create_buffered_file_writer_uint8(size_t buffer_size)` — returns a
  `std::shared_ptr<IFileWriterUint8>`
- `create_buffered_file_writer_uint16(size_t buffer_size)` — returns a
  `std::shared_ptr<IFileWriterUint16>`
- `create_buffered_file_writer_int16(size_t buffer_size)` — returns a
  `std::shared_ptr<IFileWriterInt16>` (16-bit signed PCM output, e.g. WAV
  audio)

New `IStageServices` methods are appended after existing entries (append-only
convention), so plugins built against an older SDK keep working with a newer
host within the same version pair.

Progress reporting and artifact-delivery callbacks are **not** part of the
current services contract. If your stage needs a host capability that is
missing, request an SDK extension rather than working around it.

### Optional: Stage tools

If your stage provides an interactive tool (e.g., a custom editor or analysis
view), implement `StageToolProvider` and return a vector of `StageToolDescriptor`
from `get_stage_tools()`. The host discovers these descriptors through the
presenter layer and routes them to the GUI without any hardcoded stage-name
dispatch.

## Testing

Unit-test your stage implementation in isolation. Inject a mock `IStageServices`
rather than depending on a live host. The skeleton template includes example
Google Test suites and CMake test registration.

Recommended ctest labels for plugin test suites:

| Label | Scope |
|-------|-------|
| `unit` | All fast, isolated stage unit tests |
| `sources` | Source stage tests |
| `transforms` | Transform stage tests |
| `sinks` | Sink stage tests |

## Distribution

### GitHub release assets

Publish platform binaries as GitHub release assets using the naming convention:

```
orc-plugin_<stage-name>_linux.so
orc-plugin_<stage-name>_macos.dylib
orc-plugin_<stage-name>_windows.dll
```

The skeleton CI workflows produce and upload these artifacts automatically on
tagged releases.

### Registry entry

Users register your plugin by adding an entry to their plugin registry YAML:

```yaml
- plugin_id:          com.example.my-stage
  plugin_version:     "1.0.0"
  source_repo_url:    https://github.com/example-org/orc-plugin_my-stage
  artifact_source:    github_release_asset
  release_asset_url:  https://github.com/example-org/orc-plugin_my-stage/releases/download/v1.0.0/orc-plugin_my-stage_linux.so
  release_tag:        v1.0.0
  release_asset_name: orc-plugin_my-stage_linux.so   # platform-specific
  target_platform:    linux
  required_host_abi:  6
  enabled:            true
  trust_state:        untrusted
  license_spdx:       MIT
  sha256:             9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
```

The host downloads and caches the binary automatically the first time it
starts with this entry present — but only once the entry is trusted;
entries with `trust_state: untrusted` are neither downloaded nor loaded.
Plugins added through the GUI Plugin Manager (or with
`orc-cli plugins add --trusted`) are trusted at add time; entries supplied
any other way — such as the hand-written snippet above — default to
untrusted and must be activated by enabling them in the Plugin Manager or
with `orc-cli plugins trust <id>`. Publish the artifact's SHA-256 digest
so users can record it in the optional `sha256` field: the host then
verifies the download (and every
cache hit) against it and quarantines mismatching files. Without a `sha256`
the plugin still loads, with a warning that its integrity was not verified.
Plugin binaries are not code-signed; see the "Distribution integrity"
section of
[plugin-architecture.md](plugin-architecture.md#distribution-integrity) for
exactly what is and is not verified.

## Versioning and Compatibility Policy

### Binary compatibility model

The plugin boundary is a **C++ ABI**, not a C ABI. `std::shared_ptr`,
`std::string`, STL containers, exceptions, and virtual-function tables all
cross the host/plugin boundary. Matching version numbers are therefore
necessary but not sufficient: a plugin must be built with the same compiler
family, the same C++ standard library, and a compatible build configuration
as the host. On Windows this includes the CRT flavour — a Debug-CRT plugin
cannot be loaded by a Release-CRT host.

Since ABI v5 this requirement is enforced at load time: the descriptor's
`toolchain_tag` field (populated by the `ORC_SDK_TOOLCHAIN_TAG` macro, and
automatically by `ORC_STAGE_PLUGIN_DESCRIPTOR`) encodes the compiler family
and major version, the C++ standard library, and — on Windows — the CRT
flavour, e.g. `gcc14/libstdc++`, `clang17/libc++`,
`msvc19/msvc-stl/release-crt`. The host requires the plugin's tag to equal
its own tag exactly and rejects the plugin with a diagnostic naming both
tags otherwise. The policy is deliberately conservative, consistent with the
exact-match rule for the version numbers.

The host requires **exact equality** for both `host_abi_version` and
`plugin_api_version`; a mismatch in either causes the plugin to be rejected
with a logged diagnostic. The `services_size` field in `OrcPluginServices`
is an intra-version safety net only: it guards access to service-table
fields appended within the current ABI version. It is not a cross-version
compatibility mechanism.

### Version history

| `host_abi_version` | `plugin_api_version` | Change |
|--------------------|----------------------|--------|
| 1 | — | Initial internal release; `plugin_api_version` not yet in descriptor |
| 2 | 1 | `plugin_api_version` added to `StagePluginDescriptor`; public SDK headers published |
| 3 | 1 | `OrcPluginServices` table added; `orc_register_stage_plugin` now receives `const OrcPluginServices*` as its first parameter; plugins must use the services table for logging instead of resolving host symbols directly |
| 4 | 2 | Decode-Orc 2.0: `VideoFrameRepresentation` replaces `VideoFieldRepresentation` as the primary frame-data contract; `DAGStage::execute()` operates on frame-based artifacts; `DropoutRun` replaces `DropoutRegion`; `FieldID`/`FieldIDRange` removed — use `FrameID`/`FrameIDRange`. All plugins must be rebuilt against the v2.0 SDK |
| 5 | 2 | `StagePluginDescriptor` gains the appended `toolchain_tag` field (populate with `ORC_SDK_TOOLCHAIN_TAG`); the loader requires the plugin's tag to equal the host's exactly. Registration helpers (`ORC_STAGE_PLUGIN_DESCRIPTOR`, `ORC_DEFINE_STAGE_PLUGIN`) added in `<orc/plugin/orc_plugin_registration.h>` |
| 6 | 2 | Multi-track audio: `VideoFrameRepresentation`'s single-track audio accessors (`audio_locked()`, `get_audio_sample_count(FrameID)`, `get_audio_samples(FrameID)`) are replaced by a track-indexed API — `audio_track_count()`, `get_audio_track_descriptor(track)`, per-track frame-locked accessors, and per-track free-running stream accessors (`get_audio_stream_pair_count` / `get_audio_stream_samples`). New contract header `<orc/stage/audio_track.h>` (`AudioTrackDescriptor`, `AudioSampleRate`, `AudioTrackOrigin`, `kMaxAudioTracks`, `audio_stream_pair_offset()`). The vtable layout change requires all plugins to be rebuilt |

### When the host increments a version

If `host_abi_version` increases, you must rebuild your plugin against the new
SDK. The host will refuse to load binaries built against an older ABI.

If `plugin_api_version` increases, your plugin must update its stage
implementation to match the new stage contract and recompile.

Neither increment is made without a corresponding Decode-Orc release and
migration notes.

## Repository Naming Convention

External plugin repository names should use the prefix `orc-plugin_`:

```
orc-plugin_<name>     e.g.  orc-plugin_skeleton
                            orc-plugin_example_stage
```

This convention applies to repositories hosted under the decode-orc
organisation and is recommended for all third-party plugin authors.
