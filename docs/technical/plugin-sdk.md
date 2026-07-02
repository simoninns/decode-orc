# Plugin SDK Developer Guide

This guide covers everything needed to create, build, test, and distribute a
third-party Decode-Orc stage plugin using the public Plugin SDK. No changes to
the Decode-Orc source repository are required.

## Quick Start

Clone the official skeleton template to get a working scaffold immediately:

```bash
git clone https://github.com/simoninns/orc-plugin_skeleton my-orc-plugin
cd my-orc-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

The skeleton includes a sample passthrough stage, unit tests, and
Linux/macOS/Windows CI workflows.

## SDK Headers

All plugin code must include only the public SDK umbrella header:

```cpp
#include <orc/plugin/orc_plugin_sdk.h>
```

This pulls in all stable contracts:

| Header | Provides |
|--------|----------|
| `<orc/plugin/orc_plugin_abi.h>` | `StagePluginDescriptor`, `OrcStageFactoryFn`, `ORC_STAGE_PLUGIN_EXPORT`, `kStagePluginHostAbiVersion`, `kStagePluginApiVersion`, entrypoint symbol names |
| `<orc/plugin/orc_stage_api.h>` | `ParameterizedStage`, `TriggerableStage`, `NodeTypeInfo`, `ParameterValue`, `ParameterDescriptor`, `VideoSystem`, `SourceType` |
| `<orc/plugin/orc_stage_runtime.h>` | Stage runtime include surface for stage implementations (observation context, triggerable stage) |
| `<orc/plugin/orc_stage_preview.h>` | Preview capability and carrier contracts for preview-capable stages |
| `<orc/plugin/orc_stage_services.h>` | `IStageServices`, `IFileWriterUint8`, `IFileWriterUint16` — host-provided buffered file-output factories for sink stages |
| `<orc/plugin/orc_stage_tooling.h>` | `StageToolDescriptor`, `StageToolProvider`, `AnalysisToolDescriptor`, `AnalysisToolProvider`, `ORC_STAGE_INSTRUCTIONS_MD` — optional tool contracts and stage self-documentation |
| `<orc/plugin/orc_plugin_services.h>` | `OrcPluginServices` host service table, `OrcPluginLogLevel`, `orc::plugin::set_services()`, `orc::plugin::get_stage_services()` |
| `<orc/plugin/orc_plugin_services_helpers.h>` | `ORC_PLUGIN_LOG_*` logging macros |

### Stability Guarantees

Headers in the public SDK carry an explicit stability commitment:
- Changes to ABI-level contracts require a bump to `kStagePluginHostAbiVersion`.
- Changes to stage API contracts require a bump to `kStagePluginApiVersion`.
- Incompatible plugins are refused by the host with a diagnostic message;
  they are never silently mis-loaded.

## What Plugins Must Not Include

The following are **not** part of the public SDK and will change without notice:

| Forbidden header path | Reason |
|-----------------------|--------|
| `orc/core/include/*.h` | Internal host implementation headers |
| `orc/presenters/*.h` | Presenter layer — GUI/CLI internal |
| `orc/gui/*.h` | GUI internal |
| `orc/cli/*.h` | CLI internal |

Do not link directly against `orc-core`, `orc-presenters`, `orc-gui`, or
`orc-cli`. Link only against `orc::plugin-sdk`.

If a capability you need is missing from the SDK, open an issue in the
decode-orc repository. Missing capability is never a reason to include private
headers or link private internals.

## CMake Integration

### Out-of-tree plugin project

```cmake
cmake_minimum_required(VERSION 3.20)
project(my-orc-stage CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(decode-orc-plugin-sdk REQUIRED)

orc_add_stage_plugin(
    my-orc-stage-my-filter
    OUTPUT_NAME    orc-plugin_my-filter_my-stage
    PLUGIN_VERSION "${PROJECT_VERSION}"
    SOURCES        plugin.cpp my_filter_stage.cpp
)
```

The installed `decode-orc-plugin-sdk` CMake package provides:
- `orc::plugin-sdk` — imported INTERFACE target with all SDK include paths
- `orc_add_stage_plugin()` — plugin target helper macro
- The public SDK headers
- `check_plugin_private_includes.sh` / `check_plugin_private_links.sh` — SDK enforcement gate scripts

### Validating SDK-only compliance locally

To run the same enforcement gates that execute in Decode-Orc CI/CD, use the
scripts bundled with the installed SDK package (located at
`<install-prefix>/lib/cmake/decode-orc-plugin-sdk/`):

```bash
# Check for forbidden private-header includes in your plugin tree
bash <sdk-install>/check_plugin_private_includes.sh /path/to/your-plugin-repo

# Check for forbidden private-link dependencies in your plugin target
bash <sdk-install>/check_plugin_private_links.sh /path/to/your-plugin-repo
```

Both scripts exit with code 1 and print a diagnostic on violations.  Run them
before opening a pull request or tagging a release.

### Output naming

Follow the artifact naming convention so the host can validate and cache
plugin binaries correctly:

```
orc-plugin_<stage-name>_<platform>.<ext>
```

## Implementing a Stage

### Minimal plugin structure

```cpp
#include <orc/plugin/orc_plugin_sdk.h>

#include "my_stage.h"  // Your DAGStage subclass

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

The same pattern is used by every bundled stage plugin — see
`orc/plugins/stages/mask_line/plugin.cpp` in the Decode-Orc repository for a
complete real-world example.

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

`IStageServices` currently exposes exactly two factory methods, used by sink
stages for buffered file output:

- `create_buffered_file_writer_uint8(size_t buffer_size)` — returns a
  `std::shared_ptr<IFileWriterUint8>`
- `create_buffered_file_writer_uint16(size_t buffer_size)` — returns a
  `std::shared_ptr<IFileWriterUint16>`

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
  required_host_abi:  4
  enabled:            true
  trust_state:        untrusted
  license_spdx:       MIT
```

The host downloads and caches the binary automatically the first time it starts
with this entry present.

## Versioning and Compatibility Policy

### Binary compatibility model

The plugin boundary is a **C++ ABI**, not a C ABI. `std::shared_ptr`,
`std::string`, STL containers, exceptions, and virtual-function tables all
cross the host/plugin boundary. Matching version numbers are therefore
necessary but not sufficient: a plugin must be built with the same compiler
family, the same C++ standard library, and a compatible build configuration
as the host. On Windows this includes the CRT flavour — a Debug-CRT plugin
cannot be loaded by a Release-CRT host.

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
