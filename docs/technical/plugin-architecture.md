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
- **Registry-based distribution:** Plugins are declared in a persistent YAML
  registry and can be fetched automatically from GitHub release assets at
  startup. Non-core registry entries must be marked trusted before they are
  downloaded or loaded — entries added through the GUI or with
  `orc-cli plugins add --trusted` are trusted at add time, while entries
  that arrive from outside the application default to untrusted — and
  downloaded artifacts are verified against a recorded SHA-256 checksum (see
  [Distribution integrity](#distribution-integrity)). Plugin binaries are
  **not** code-signed.

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
   │      ├─ Check trust_state (untrusted non-core entries are skipped
   │      │      with a warning diagnostic; nothing is downloaded or loaded)
   │      ├─ Resolve local path (download from GitHub releases if absent;
   │      │      verify sha256 checksum, quarantining mismatches)
   │      ├─ dlopen / LoadLibrary the shared library
   │      ├─ Resolve entrypoints:
   │      │      orc_get_stage_plugin_descriptor
   │      │      orc_register_stage_plugin
   │      ├─ Validate host_abi_version, plugin_api_version, toolchain_tag
   │      └─ Call orc_register_stage_plugin → StageRegistry::register_stage
   │
  ├─ Merge registry paths, default search paths, and ORC_STAGE_PLUGIN_PATHS
  │
  └─ Consumer flows (GUI / CLI) query the same StageRegistry via presenters
```

Loading happens once at startup. Hot-reload is not supported in the current
version.

Plugins are loaded with `RTLD_LOCAL` (each plugin's symbols stay private to
that plugin), and plugin libraries are reference-counted: every registered
stage factory and every live stage instance holds a keep-alive reference, so
a plugin's code is never unmapped while one of its stages can still run.

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

Each plugin must export two C-linkage entrypoints (C++ types cross the
boundary — see Binary Compatibility Model below):

```cpp
// Returns the plugin descriptor (ABI version, API version, toolchain tag,
// metadata).
// All descriptor pointer fields must reference static storage.
const orc::StagePluginDescriptor* orc_get_stage_plugin_descriptor();

// Receives the host service table and registers one or more stage types
// with the host by invoking register_stage once per exported stage.
// Returns true when every stage registered successfully.
bool orc_register_stage_plugin(
    const orc::OrcPluginServices* services, void* context,
    bool (*register_stage)(void* context, const char* stage_name,
                           orc::OrcStageFactoryFn factory),
    const char** error_message);
```

Both symbols are declared with `ORC_STAGE_PLUGIN_EXPORT` (C linkage, default
visibility). Stage factories match `orc::OrcStageFactoryFn` and return
`std::shared_ptr<orc::DAGStage>`.

## Binary Compatibility Model

The plugin boundary is a **C++ ABI**, not a C ABI. `std::shared_ptr`,
`std::string`, STL containers, exceptions, and virtual-function tables all
cross the host/plugin boundary. Matching version numbers are therefore
necessary but not sufficient: a plugin must be built with the same compiler
family, the same C++ standard library, and a compatible build configuration
as the host. On Windows this includes the CRT flavour — a Debug-CRT plugin
cannot be loaded by a Release-CRT host.

Since ABI v5 the toolchain requirement is enforced at load time: the
descriptor's `toolchain_tag` field (populated by the `ORC_SDK_TOOLCHAIN_TAG`
macro) encodes the compiler family and major version, the C++ standard
library, and — on Windows — the CRT flavour, e.g. `gcc14/libstdc++`,
`clang17/libc++`, `msvc19/msvc-stl/release-crt`. The host requires the
plugin's tag to equal its own tag exactly and rejects the plugin with a
diagnostic naming both tags otherwise.

The host requires **exact equality** for both `host_abi_version` and
`plugin_api_version`; a mismatch in either causes the plugin to be rejected
with a logged diagnostic. The `services_size` field in `OrcPluginServices`
is an intra-version safety net only: it guards access to service-table
fields appended within the current ABI version. It is not a cross-version
compatibility mechanism.

## Compatibility Gating

Two version numbers plus the toolchain tag govern compatibility. All three
are checked before a plugin is accepted; a mismatch causes the plugin to be
skipped with a logged diagnostic. For guidance on which changes force a
`host_abi_version` bump, see the
[ABI impact decision table](plugin-sdk.md#abi-impact-decision-table).

### `host_abi_version`

Controls the binary ABI: the layout of `StagePluginDescriptor`, the entrypoint
signatures, and the `register_stage` callback contract.

**Current value:** `8` (`VideoFrameRepresentation` gained
`prime_audio_decode()`, an appended virtual that forces a deferred
whole-stream audio decode to run up front with progress reporting — changing
the vtable layout; all plugins must be rebuilt). The authoritative per-version
change log is `orc/sdk/abi_history.yaml`, rendered as the version-history table
in [plugin-sdk.md](plugin-sdk.md#version-history).

Bumped when any of the following change:
- `StagePluginDescriptor` field order or alignment
- Entrypoint function signatures
- Callback calling convention
- `IStageServices` gains or loses methods
- The vtable layout of a contract type crossing the boundary (e.g.
  `VideoFrameRepresentation`) changes

### `plugin_api_version`

Controls the stage contract: the `DAGStage` virtual interface,
`ParameterizedStage`, `TriggerableStage`, `ArtifactPtr`, `ObservationContext`,
and `NodeTypeInfo` semantics.

**Current value:** `2`

Bumped when any of the following change:
- A `DAGStage` virtual method is added, removed, or reordered
- `ParameterValue` variant types change
- `NodeTypeInfo` struct layout changes
- `execute()` or `trigger()` lifecycle semantics change incompatibly
- The primary frame-data type changes (e.g. field → frame representation)

### `toolchain_tag`

Identifies the build environment the plugin binary was produced with (ABI
v5+). Compared as an exact string against the host's own tag; see the
Binary Compatibility Model above for the encoding.

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
| `trust_state` | Trust level, enforced before loading: entries other than `trusted` are neither downloaded nor `dlopen`ed unless `is_core_plugin` is set. Trust is a distinct decision from adding or enabling: adding a plugin (local file, URL, or from the curated index) records the entry but leaves it `untrusted` unless the user confirms trust in the explicit confirmation dialog, and toggling the **Enabled** checkbox never changes trust. Trust is granted through the Plugin Manager's **Trusted** column (with a confirmation dialog) or `orc-cli plugins trust <id>` / `untrust <id>`; entries that arrive from outside the application (e.g. a hand-edited registry file) also default to `untrusted` |
| `license_spdx` | SPDX license identifier |
| `is_core_plugin` | Marks entries supplied by Decode-Orc itself; implicitly trusted |
| `required_host_abi` | Host ABI the plugin was built for. Enforced before download and load: a non-zero value that does not equal the host's `host_abi_version` means the entry is neither downloaded nor `dlopen`ed — it stays visible with a "needs a rebuild for Orc ABI N" message in `orc-cli plugins list` and the GUI Plugin Manager. `0` means unspecified (not gated); `is_core_plugin` entries are exempt |
| `sha256` | Optional SHA-256 digest (64 hex chars) of the plugin binary for `github_release_asset` entries; verified after download and on cache hits |

Entries with `artifact_source: github_release_asset` and an absent or empty
`path` are resolved automatically: the host downloads the binary from the
declared GitHub release and caches it to
`~/.config/decode-orc/plugin-cache/<platform>/` before loading. The download
only happens for trusted entries. When the entry records a `sha256`, the
digest is checked both after a fresh download and on every cache hit; a
mismatching file is quarantined (renamed with a `.quarantined` suffix) and
reported as an error, and a mismatching cache hit triggers one fresh
download attempt. When no `sha256` is recorded, the host loads the artifact
but emits a warning that its integrity could not be verified.

## Curated plugin index

Alongside manual URL entry, the host offers a **curated index** of third-party
plugins for discovery and one-click install. The index is a versioned YAML
document read from a configurable URL (default: the `orc-plugin-registry/`
`index.yaml` on the Decode-Orc default branch; override with the
`ORC_PLUGIN_INDEX_URL` environment variable). Because the host reads the branch
head over plain HTTPS, a merged registry change publishes immediately — no host
release and no registry tag are required.

The host refreshes the index on demand — when the Plugin Manager's **Browse
Plugins…** dialog opens or a `orc-cli plugins search / info / install` command
runs — asynchronously, falling back to the last-good cached copy
(`<config>/plugin-index-cache.yaml`) when offline.

Schema (`registry_schema: 1`):

| Field | Description |
|-------|-------------|
| `registry_schema` | Index schema **major** version. Hosts ignore unknown fields, so additions within a major are non-breaking; a newer major is parsed best-effort so an older host still resolves compatible builds |
| `plugins[].id` | Unique plugin identifier |
| `plugins[].display_name` | Human-readable name |
| `plugins[].description` | Short description |
| `plugins[].tags` | Search tags |
| `plugins[].maintainer` | Maintainer name |
| `plugins[].license_spdx` | SPDX license identifier (mandatory) |
| `plugins[].source_repo_url` | Plugin source repository |
| `plugins[].artifacts[]` | One build per (platform, host ABI) |
| `artifacts[].platform` | `linux`, `macos`, or `windows` (a more specific value such as `linux-x86_64` is matched by prefix) |
| `artifacts[].host_abi` | Host ABI this build targets |
| `artifacts[].url` | Direct release-asset download URL |
| `artifacts[].sha256` | Mandatory 64-hex digest, carried into the local registry on install |
| `artifacts[].plugin_version` | Plugin release version |
| `artifacts[].min_host_app_version` | Minimum host application version (optional) |

Artifacts are resolved by **(platform, host ABI)** using the same compatibility
gating as the registry ([Compatibility Gating](#compatibility-gating)): an
older host that finds no matching build reports "no build for this host" before
downloading anything, instead of failing at `dlopen`. Installing from the index
records a registry entry carrying the index's `sha256`, left **untrusted**
until the user confirms trust.

Contribution model: plugin authors open a pull request adding their entry;
repository CI validates every PR (schema conformance, artifact naming including
the ABI token, URL reachability, digest match, and a present SPDX license), and
a maintainer's merge is the curation and trust decision. See
[`orc-plugin-registry/README.md`](../../orc-plugin-registry/README.md).

### Distribution integrity

What the host verifies before running plugin code, and what it does not:

**Verified:**

- `trust_state` — untrusted non-core registry entries are never downloaded
  or `dlopen`ed (`stage_plugin_registry.cpp`, `stage_registry.cpp`).
- `sha256` — downloaded artifacts (and cached copies) are checked against
  the registry digest when one is recorded (`plugin_remote_loader.cpp`).
- `host_abi_version` and `plugin_api_version` — exact-match at load time
  (`stage_plugin_loader.cpp`).
- `required_host_abi` — a non-zero registry value that does not match the
  host ABI blocks download and load early, before any bytes are fetched
  (`stage_plugin_registry.cpp`).
- `toolchain_tag` — exact-match against the host's compiler/stdlib/build
  configuration tag (ABI v5).

**What the curated index adds:**

- **Mandatory digests.** Every index artifact must carry a `sha256`, enforced
  by the registry PR validation before merge; installing from the index copies
  that digest into the local registry so the download is always integrity-
  checked (unlike a hand-entered URL, where a digest is optional).
- **Curated review.** A human maintainer's merge is the trust decision for a
  listed plugin: the PR workflow verifies the artifact URL resolves and its
  bytes hash to the declared digest before the entry can go live.

**Not verified (future work):**

- **Signed index.** The index itself is fetched over HTTPS but is not
  cryptographically signed, so its authenticity rests on transport security and
  repository access control. Signing the index (e.g. minisign/sigstore) is a
  documented follow-on.
- **Code signing.** Plugin binaries carry no cryptographic signature; the
  `sha256` authenticates an artifact only as strongly as the index or registry
  entry that records it.
- **Unsigned local registry.** The on-disk registry
  (`stage-plugins.yaml`) and the cached index copy are plain files with no
  signature. An attacker who can write to the user's config directory could
  flip `trust_state` or point an entry at a malicious binary with a matching
  digest; the host trusts the local registry as much as the filesystem it lives
  on. Confining that directory's permissions is the user's responsibility until
  registry signing lands.

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
orc-plugin_<stage-name>_<platform>[_abi<N>].<ext>
```

The optional `_abi<N>` token records the host ABI version the binary targets,
so one release can carry builds for several host ABIs. The host prefers the
asset tagged for its own ABI, falls back to a legacy (untagged) name — which
the load-time gate still validates — and reports "needs a rebuild" rather than
downloading an asset tagged for an incompatible ABI. Untagged names remain
valid.

Examples:
- `orc-plugin_skeleton_passthrough_linux.so` (legacy, untagged)
- `orc-plugin_skeleton_passthrough_linux_abi8.so` (ABI-tagged)
- `orc-plugin_skeleton_passthrough_macos_abi8.dylib`
- `orc-plugin_skeleton_passthrough_windows_abi8.dll`

External plugin repository names follow the same prefix convention
(`orc-plugin_<name>`), both for official decode-orc organization repositories
and as the recommended standard for third-party authors.

## Stage Services

Plugins interact with the host through explicit service interfaces rather than
direct calls into host internals. The host builds an `OrcPluginServices` table
(declared in `<orc/plugin/orc_plugin_services.h>`) and passes it as the first
argument to `orc_register_stage_plugin()`. The table provides:

- `log` — pre-formatted message logging routed to the host logger (plugins use
  the `ORC_PLUGIN_LOG_*` macros)
- `render_colour_preview` — converts a decoded `ColourFrameCarrier` to a
  display-ready `PreviewImage`
- `stage_services` — optional pointer to the consolidated `IStageServices`
  interface (may be `nullptr` when the capability is unavailable)

The `IStageServices` contract (declared in `<orc/plugin/orc_stage_services.h>`)
currently exposes buffered file-output factories used by sink stages:
`create_buffered_file_writer_uint8()`, `create_buffered_file_writer_uint16()`,
and `create_buffered_file_writer_int16()`. It does not currently provide
artifact delivery, logging, or progress reporting.

Plugins store the table with `orc::plugin::set_services()` at the start of
`orc_register_stage_plugin`, and obtain the `IStageServices` pointer later via
`orc::plugin::get_stage_services()` (which returns `nullptr` when the host does
not provide it).

## Stage Tools

Stages that expose interactive tooling (custom editors, analysis views) publish
optional `StageToolDescriptor` records through the `StageToolProvider` mixin
(declared in `<orc/plugin/orc_stage_tooling.h>`). The host discovers and routes
these descriptors through the presenter layer — no hardcoded stage-name branches
exist in host tool dispatch.

Analysis tools (dropout analysis, SNR analysis, burst-level analysis) follow the
same pattern via `AnalysisToolDescriptor` and `AnalysisToolProvider`.

## SDK Boundary Enforcement

The source-level plugin boundary is an **allowlist** of SDK contract
headers: the `<orc/plugin/...>` family (ABI, entrypoints, services) and the
`<orc/stage/...>` family (stage, frame, observation, preview, and utility
contracts). The complete permitted set is listed in the
[Plugin SDK Developer Guide](plugin-sdk.md) (SDK Headers section); anything
else in the host tree is private.

Enforcement happens at two levels:

1. **Compile time** — the `orc-plugin-sdk` / `orc::plugin-sdk` target
   propagates only the SDK include tree, the spdlog/fmt usage requirements the
   SDK headers themselves need, and the `orc-sdk-support` static library
   (support-tier helper symbols). The host libraries are **not** linked — the
   former `$<LINK_ONLY:orc-core>` tether was removed so plugins compile and link
   against the SDK alone. Private host include directories and third-party
   dependencies are therefore invisible to plugin translation units; including a
   private host header fails the plugin's compile. Third-party libraries a
   plugin uses directly must be declared by the plugin's own CMake target.
2. **Scan gates** — two hard-fail CI gates (`ctest -L sdk`) run on the
   in-tree plugin tree `orc/plugins/stages/`:
   `check_plugin_private_includes.sh` fails on any include that is not an
   allowlisted SDK header, a plugin-local header, a standard-library or
   platform header, or a permitted third-party header;
   `check_plugin_private_links.sh` fails on plugin build files that link
   private host targets directly. Third-party authors run the same scripts
   in standalone mode against their own repository (see
   [Publishing a plugin](../technical/plugin-publishing.md)).

## Third-Party Plugin Repositories

An official skeleton template lives at
[simoninns/orc-plugin_skeleton](https://github.com/simoninns/orc-plugin_skeleton).
It provides:

- Minimal buildable plugin scaffold (CMake + SDK-only includes + sample stage)
- Unit tests
- Linux / macOS / Windows CI workflows
- Packaging conventions
- SPDX / licensing guidance

Use it as the starting point for any new out-of-tree plugin. For a
step-by-step walkthrough from an empty directory to a loaded plugin, see the
[Plugin Author Guide](../technical/plugin-author-guide.md); for release and
registry submission, see the
[Plugin Publishing Guide](../technical/plugin-publishing.md).
