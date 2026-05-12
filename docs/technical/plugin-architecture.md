# Plugin Architecture

Decode-Orc uses a **plugin-first stage system** where every processing stage —
including the stages shipped with Decode-Orc itself — is loaded at runtime through
the same plugin framework. There is no privileged path for built-in stages; all
stages are self-contained shared libraries that are discovered, loaded, and
registered through a common host runtime.

## Core Goals

- **Uniform loading:** Decode-Orc-supplied stages and third-party stages follow
  identical contracts, loader paths, and registry entries.
- **Independent development:** A third-party plugin can be developed, built, and
  distributed without any changes to the Decode-Orc source repository.
- **Stable binary interface:** Explicit ABI and API version numbers govern
  compatibility, and the host refuses to load mismatched plugins with a clear
  diagnostic.
- **Signed-registry distribution:** Plugins are declared in a persistent YAML
  registry and can be fetched automatically from GitHub release assets at startup.

## Runtime Flow

```
Host startup
   │
  ├─ Read user plugin registry YAML
  │      ($XDG_CONFIG_HOME/decode-orc/stage-plugins.yaml or
  │       ~/.config/decode-orc/stage-plugins.yaml)
  │
  ├─ Collect default plugin search paths
  │      (development build dir or executable-relative install dir)
   │
   ├─ For each registry entry
   │      ├─ Resolve local path (download from GitHub releases if absent)
   │      ├─ dlopen / LoadLibrary the shared library
   │      ├─ Resolve entrypoints:
   │      │      orc_get_stage_plugin_descriptor
   │      │      orc_register_stage_plugin
   │      ├─ Validate host_abi_version and plugin_api_version
   │      └─ Call orc_register_stage_plugin → StageRegistry::register_stage
   │
  ├─ Merge registry paths, default search paths, and ORC_STAGE_PLUGIN_PATHS
  │
  └─ Consumer flows (GUI / CLI) query the same StageRegistry via presenters
```

Loading happens once at startup. Hot-reload is not supported in the current
version.

`orc-gui` and `orc-cli` share the same persistent registry file. The difference
between development and packaged installs is the default plugin search path, not
a separate per-application registry.

## Plugin Binary Format

Plugins are native shared libraries:

| Platform | Extension |
|----------|-----------|
| Linux    | `.so`     |
| macOS    | `.dylib`  |
| Windows  | `.dll`    |

Each plugin must export two C-linkage entrypoints:

```c
// Returns the plugin descriptor (ABI version, API version, metadata)
const StagePluginDescriptor* orc_get_stage_plugin_descriptor(void);

// Registers one or more stage types with the host
void orc_register_stage_plugin(OrcStageRegisterFn register_fn, void* host_ctx);
```

## Compatibility Gating

Two version numbers govern compatibility. Both are checked before a plugin is
accepted; a mismatch causes the plugin to be skipped with a logged diagnostic.

### `host_abi_version`

Controls the binary ABI: the layout of `StagePluginDescriptor`, the entrypoint
signatures, and the `register_stage` callback contract.

**Current value:** `3`

Bumped when any of the following change:
- `StagePluginDescriptor` field order or alignment
- Entrypoint function signatures
- Callback calling convention

### `plugin_api_version`

Controls the stage contract: the `DAGStage` virtual interface,
`ParameterizedStage`, `TriggerableStage`, `ArtifactPtr`, `ObservationContext`,
and `NodeTypeInfo` semantics.

**Current value:** `1`

Bumped when any of the following change:
- A `DAGStage` virtual method is added, removed, or reordered
- `ParameterValue` variant types change
- `NodeTypeInfo` struct layout changes
- `execute()` or `trigger()` lifecycle semantics change incompatibly

## Plugin Registry

The registry is a YAML file that tracks the installed plugin set. The current
runtime stores it at:

- Linux: `$XDG_CONFIG_HOME/decode-orc/stage-plugins.yaml` or `~/.config/decode-orc/stage-plugins.yaml`
- macOS: `~/.config/decode-orc/stage-plugins.yaml` unless `XDG_CONFIG_HOME` is set
- Windows: `%APPDATA%/decode-orc/stage-plugins.yaml`

If none of those platform defaults are available, the host falls back to
`.decode-orc/stage-plugins.yaml` under the current working directory.

Both `orc-gui` and `orc-cli` read and write this same registry file.

Each entry records:

| Field | Description |
|-------|-------------|
| `plugin_id` | Unique string identifier |
| `plugin_version` | Plugin release version |
| `path` | Resolved local path to the plugin binary |
| `source_repo_url` | Repository URL for the plugin source or release |
| `artifact_source` | `local_path` or `github_release_asset` |
| `release_asset_url` | Direct URL to the GitHub release asset |
| `release_tag` | Release tag associated with the asset |
| `release_asset_name` | Expected artifact filename |
| `target_platform` | Optional platform hint for cache selection |
| `local_dev_path` | Optional development override used before remote download |
| `enabled` | Whether the plugin is loaded at startup |
| `trust_state` | Trust level (`untrusted`, etc.) |
| `license_spdx` | SPDX license identifier |
| `is_core_plugin` | Marks entries supplied by Decode-Orc itself |
| `required_host_abi` | Expected host ABI version |

Entries with `artifact_source: github_release_asset` and an absent or empty
`path` are resolved automatically: the host downloads the binary from the
declared GitHub release and caches it to
`~/.config/decode-orc/plugin-cache/<platform>/` before loading.

## Project-Level Plugin Metadata

Project files may also include a root-level `required_plugins` block. This is a
snapshot of the subset of third-party plugin registry metadata that is actually
required by the current project DAG.

Each `required_plugins` entry stores:

| Field | Description |
|-------|-------------|
| `plugin_id` | Plugin identifier expected by the project |
| `plugin_version` | Last known plugin version used when saved |
| `source_repo_url` | Repository URL for the plugin source or release |
| `artifact_source` | `local_path` or `github_release_asset` |
| `release_asset_url` | Direct release asset URL when known |
| `release_tag` | Release tag associated with the saved metadata |
| `release_asset_name` | Expected artifact filename |
| `target_platform` | Optional platform hint |
| `local_dev_path` | Optional development override path |
| `license_spdx` | SPDX license identifier |
| `is_core_plugin` | Whether the plugin is Decode-Orc supplied |
| `required_host_abi` | Host ABI version expected by the plugin |
| `stage_names` | Stage names from that plugin that are still referenced by the project |

The host rewrites this block on every save. It only keeps entries whose
`stage_names` are still referenced by the project, so stale third-party plugin
references are removed automatically when plugin-backed stages are deleted.

When a project references a missing runtime stage, the loader consults this
saved block to enrich the error message with the expected plugin id and, when
available, a repository or release URL.

In addition to registry entries, the host also searches the standard plugin
install/build locations relative to the executable:

- Linux install: `../lib/orc-stage-plugins`
- macOS install: `../PlugIns/orc-stage-plugins`
- Windows install: `orc-stage-plugins`
- Development builds: the compiled-in build plugin directory when the
  executable-relative install path does not exist

## Artifact Naming Convention

All plugin release artifacts follow a consistent naming scheme:

```
orc-plugin_<stage-name>_<platform>.<ext>
```

Examples:
- `orc-plugin_skeleton_passthrough_linux.so`
- `orc-plugin_skeleton_passthrough_macos.dylib`
- `orc-plugin_skeleton_passthrough_windows.dll`

External plugin repository names follow the same prefix convention
(`orc-plugin_<name>`), both for official decode-orc organization repositories
and as the recommended standard for third-party authors.

## Stage Services

Plugins interact with the host through explicit service interfaces rather than
direct calls into host internals. The `IStageServices` contract (declared in
`<orc/plugin/orc_stage_services.h>`) exposes:

- Artifact I/O callbacks (field/frame delivery)
- Logging and diagnostics
- Progress reporting

The host provides a concrete `IStageServices` implementation at registration
time. Plugins call `orc::plugin::get_stage_services()` to obtain the pointer
within their `orc_register_stage_plugin` implementation.

## Stage Tools

Stages that expose interactive tooling (custom editors, analysis views) publish
optional `StageToolDescriptor` records through the `StageToolProvider` mixin
(declared in `<orc/plugin/orc_stage_tooling.h>`). The host discovers and routes
these descriptors through the presenter layer — no hardcoded stage-name branches
exist in host tool dispatch.

Analysis tools (dropout analysis, SNR analysis, burst-level analysis) follow the
same pattern via `AnalysisToolDescriptor` and `AnalysisToolProvider`.

## Third-Party Plugin Repositories

An official skeleton template lives at
[simoninns/orc-plugin_skeleton](https://github.com/simoninns/orc-plugin_skeleton).
It provides:

- Minimal buildable plugin scaffold (CMake + SDK-only includes + sample stage)
- Unit tests
- Linux / macOS / Windows CI workflows
- Packaging conventions
- SPDX / licensing guidance

Use it as the starting point for any new out-of-tree plugin.
