# Plugin Publishing Guide

How to package, release, and distribute a third-party Decode-Orc stage plugin so
users can install it — either from a hand-written registry entry or, preferably,
from the curated plugin index.

This is the release-side companion to the
[Plugin Author Guide](plugin-author-guide.md) (which covers building and loading
a plugin locally). Reference material lives in
[Plugin SDK Developer Guide → Distribution](plugin-sdk.md#distribution) and
[Plugin Architecture](plugin-architecture.md).

---

## 1. Repository naming

Host your plugin in its own repository named with the `orc-plugin_` prefix:

```
orc-plugin_<stage-name>     e.g.  orc-plugin_deinterlace
```

This convention is used across the ecosystem and matches the release-asset
naming below.

---

## 2. Release-asset naming (ABI-tagged)

Publish platform binaries as GitHub release assets using:

```
orc-plugin_<stage-name>_<platform>[_abi<N>].<ext>
```

where `<platform>` is `linux` / `macos` / `windows`, `<ext>` is
`so` / `dylib` / `dll`, and the optional `_abi<N>` token records the **host ABI**
the binary was built against:

```
orc-plugin_deinterlace_linux_abi8.so       # ABI-tagged (recommended)
orc-plugin_deinterlace_macos_abi8.dylib
orc-plugin_deinterlace_windows_abi8.dll
orc-plugin_deinterlace_linux.so            # legacy, untagged (still valid)
```

Tag your assets with the ABI token. It lets a single release ship builds for
several host ABI versions side by side: when resolving a release the host
prefers the asset tagged for its own ABI (`_abi<host>`), falls back to a legacy
untagged name (validated at load), and only selects a differently-tagged asset
as a last resort — reporting "needs a rebuild for Orc ABI N" *before* it
downloads a binary it cannot load.

Find the ABI number you built against in the
[SDK version history](plugin-sdk.md#version-history) (`host_abi_version`).

The skeleton CI workflows produce and upload these artifacts automatically on
tagged releases.

---

## 3. Generate and publish SHA-256 digests

Digests let the host verify every download and every cache hit, and quarantine
mismatching files. They are **optional** in a hand-written registry entry (a
missing digest loads with an integrity warning) but **mandatory** for a curated
index entry.

Compute the digest of each asset and publish it alongside the release:

```bash
sha256sum orc-plugin_deinterlace_linux_abi8.so
# 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08  orc-plugin_...
```

(On macOS: `shasum -a 256 <file>`.) Record the hex digest in the entry's
`sha256` field.

> Plugin binaries are **not code-signed**. See
> [Distribution integrity](plugin-architecture.md#distribution-integrity) for
> exactly what the digest does and does not guarantee, and the roadmap toward a
> signed index.

---

## 4. Rebuild expectations on host ABI bumps

The plugin boundary is a **C++ ABI**, not a C ABI — vtables, `std::shared_ptr`,
`std::string`, and STL containers cross it. Matching version numbers are
necessary but not sufficient: the plugin must be built with the same compiler
family, C++ standard library, and build configuration as the host (encoded in
the descriptor's `toolchain_tag` and checked for exact equality at load).

Consequences for publishing:

- **When `host_abi_version` increases**, you must rebuild against the new SDK
  and publish a new asset tagged `_abi<new>`. The host refuses to load binaries
  built against an older ABI. Keep the older-ABI asset in the release so users
  on older hosts still have a compatible build.
- **When `plugin_api_version` increases**, update your stage implementation to
  the new contract and recompile.
- Neither increment happens without a corresponding Decode-Orc release and
  migration notes. Watch the
  [version history](plugin-sdk.md#version-history) and the
  [ABI impact decision table](plugin-sdk.md#abi-impact-decision-table).

Before tagging a release, run the SDK enforcement gates in standalone mode
against your repository (see
[Plugin Author Guide §6](plugin-author-guide.md#6-run-the-sdk-enforcement-gates-standalone)).

---

## 5. Distribute via a hand-written registry entry

Users can register your plugin directly by adding an entry to their plugin
registry YAML:

```yaml
- plugin_id:          com.example.deinterlace
  plugin_version:     "1.0.0"
  source_repo_url:    https://github.com/example/orc-plugin_deinterlace
  artifact_source:    github_release_asset
  release_asset_url:  https://github.com/example/orc-plugin_deinterlace/releases/download/v1.0.0/orc-plugin_deinterlace_linux_abi8.so
  release_tag:        v1.0.0
  release_asset_name: orc-plugin_deinterlace_linux_abi8.so
  target_platform:    linux
  required_host_abi:  8
  enabled:            true
  trust_state:        untrusted
  license_spdx:       GPL-3.0-or-later
  sha256:             9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
```

Set `required_host_abi` so an incompatible entry is reported (and neither
downloaded nor loaded) instead of failing late at `dlopen`. Entries default to
`trust_state: untrusted`; the host downloads and loads only after the user
grants trust (Plugin Manager **Trusted** column, or `orc-cli plugins trust
<id>`). See [Registry entry](plugin-sdk.md#registry-entry).

---

## 6. Distribute via the curated index (recommended)

The curated index is the list users browse under **Plugin Manager → Browse
Plugins…** and query with `orc-cli plugins search / info / install`. It lives in
[`orc-plugin-registry/`](../../orc-plugin-registry/) and the host fetches it from
the default-branch head over HTTPS — **a merged pull request publishes
immediately**, with no Decode-Orc release and no registry tag.

Submit an entry:

1. Fork the registry and append your plugin under `plugins:` in `index.yaml`,
   following [`registry_schema: 1`](../../orc-plugin-registry/README.md):

   ```yaml
   - id: com.example.deinterlace
     display_name: Example Deinterlacer
     description: Motion-adaptive deinterlacing for PAL/NTSC
     maintainer: Example Author
     license_spdx: GPL-3.0-or-later
     source_repo_url: https://github.com/example/orc-plugin_deinterlace
     tags: [transform, video]
     artifacts:                       # one per (platform, host ABI) you support
       - platform: linux
         host_abi: 8
         url: https://github.com/example/orc-plugin_deinterlace/releases/download/v1.0.0/orc-plugin_deinterlace_linux_abi8.so
         sha256: 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08
         plugin_version: 1.0.0
         min_host_app_version: 1.0.0  # optional
   ```

2. Provide one artifact per **(platform, host ABI)** you support. The host
   selects by that pair, so a user on an unsupported host is told "no build for
   this host" rather than downloading an incompatible binary.

3. Validate locally before opening the PR:

   ```bash
   # Offline: schema, naming, license, digest presence
   python3 orc-plugin-registry/tools/validate_index.py orc-plugin-registry/index.yaml

   # Online as well: URL reachability + digest match
   python3 orc-plugin-registry/tools/validate_index.py --online orc-plugin-registry/index.yaml
   ```

4. Open the pull request. CI validates schema conformance, artifact naming
   (including the ABI token), that every `url` resolves, that each downloaded
   artifact's digest equals its `sha256`, and that a valid SPDX `license_spdx`
   is present. Only GPLv3-compatible licenses are accepted.

**A maintainer's merge is the curation and trust decision** and publishes the
entry. Users then install with:

```console
$ orc-cli plugins search deinterlace
$ orc-cli plugins info com.example.deinterlace
$ orc-cli plugins install com.example.deinterlace   # recorded untrusted
$ orc-cli plugins trust com.example.deinterlace      # user confirms trust
```

Installing from the index records the entry with the index-declared `sha256`
and leaves it untrusted until the user confirms — the same trust flow as the GUI
**Browse Plugins…** dialog.

---

## 7. Checklist

- [ ] Repository named `orc-plugin_<stage-name>`.
- [ ] Release assets named `orc-plugin_<stage>_<platform>_abi<N>.<ext>`.
- [ ] SHA-256 digest computed and published for every asset.
- [ ] Rebuilt and re-tagged for each supported host ABI; older-ABI assets kept.
- [ ] `instructions.md` shipped beside each binary and up to date.
- [ ] Enforcement gates pass in standalone mode.
- [ ] Curated-index PR validated locally, one artifact per (platform, host ABI).
