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
| `<orc/plugin/orc_stage_services.h>` | `IStageServices` — canonical host-provided stage services (artifact I/O, logging, progress) |
| `<orc/plugin/orc_stage_tooling.h>` | `StageToolDescriptor`, `StageToolProvider`, `AnalysisToolDescriptor`, `AnalysisToolProvider` — optional interactive tool contracts |

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
#include "my_stage.h"   // Your DAGStage subclass

// Plugin descriptor
static const orc::StagePluginDescriptor kDescriptor {
    .host_abi_version  = orc::kStagePluginHostAbiVersion,
    .plugin_api_version = orc::kStagePluginApiVersion,
    .plugin_id         = "com.example.my-stage",
    .plugin_version    = "1.0.0",
    .license           = "MIT",
};

ORC_STAGE_PLUGIN_EXPORT
const orc::StagePluginDescriptor* orc_get_stage_plugin_descriptor() {
    return &kDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT
void orc_register_stage_plugin(orc::OrcStageRegisterFn register_fn,
                                void* host_ctx)
{
    auto services = orc::plugin::get_stage_services(host_ctx);
    register_fn("my.stage.id", host_ctx, [services]() {
        return std::make_unique<MyStage>(services);
    });
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

### Parameters

`ParameterizedStage` exposes `get_parameters()` returning a list of
`ParameterDescriptor` records, and `set_parameter()` / `get_parameter()` for
runtime value access. Use `ParameterValue` (a `std::variant`) to hold typed
values: `bool`, `int64_t`, `double`, `std::string`, and enumerated choices.

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

Obtain `IStageServices` via `orc::plugin::get_stage_services(host_ctx)` during
`orc_register_stage_plugin`. Use its methods for:

- `log_message()` — structured diagnostic output routed through the host logger
- `report_progress()` — progress updates surfaced in the GUI/CLI
- Artifact delivery callbacks (source stages)

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

### Version history

| `host_abi_version` | `plugin_api_version` | Change |
|--------------------|----------------------|--------|
| 1 | — | Initial internal release; `plugin_api_version` not yet in descriptor |
| 2 | 1 | `plugin_api_version` added to `StagePluginDescriptor`; public SDK headers published |
| 3 | 1 | `OrcPluginServices` table added; `orc_register_stage_plugin` now receives `const OrcPluginServices*` as its first parameter; plugins must use the services table for logging instead of resolving host symbols directly |
| 4 | 2 | Decode-Orc 2.0: `VideoFrameRepresentation` replaces `VideoFieldRepresentation` as the primary frame-data contract; `IStageServices` gains VFrameR delivery methods; `DAGStage::execute()` receives `VideoFrameRepresentationPtr` (frame-based); `DropoutRun` replaces `DropoutRegion`; `FieldID`/`FieldIDRange` removed — use `FrameID`/`FrameIDRange` |

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
