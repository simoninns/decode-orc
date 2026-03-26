# Plan: Merge Documentation into the Code Repo

**Goal:** Merge `decode-orc-docs` into this repository under `docs/` so the local AI can read and write documentation alongside code, while keeping documentation PRs simple and low-friction for non-technical contributors — achieved through GitHub's own tooling rather than a separate repository.

**Why not a submodule?** Submodules require a two-step commit workflow that AI agents get wrong, add CI sync complexity, and solve a problem (protecting non-technical contributors from code complexity) that is already handled by GitHub's web UI and CODEOWNERS. See `local-docs/include-docs-plan.md` revision history for the full analysis.

---

## Design Summary

```
decode-orc/ (single repo)
└── docs/               ← documentation lives here, as ordinary files
    ├── user-guide/
    ├── contributing/
    └── ...

Non-technical contributor:
  github.com → Browse docs/ → Click ✏ → Edit → Open PR
  GitHub web editor or github.dev (press .) — no clone, no build system, no C++ knowledge needed.
  CODEOWNERS routes docs/** PRs to a docs reviewer automatically.
  CI skips the build entirely for docs-only changes.

AI / developer:
  docs/ is just a directory — full read/write, same workflow as any other file.
```

---

## Phase 1: Import the Docs Repo History

_Bring `decode-orc-docs` into this repo under `docs/` preserving full commit history. One-time operation._

### 1.1 Fetch the docs repo as a remote and merge its history

```bash
cd /path/to/decode-orc   # this repo

# Add the docs repo as a temporary remote
git remote add docs-origin https://github.com/simoninns/decode-orc-docs
git fetch docs-origin

# Merge its history into this repo, placing all its files under docs/
# git-filter-repo is the recommended tool; it rewrites paths in the imported history
# Install: nix shell nixpkgs#git-filter-repo  (or: pip install git-filter-repo)
git fetch docs-origin main
git checkout -b import/docs docs-origin/main
git filter-repo --to-subdirectory-filter docs/ --force
git checkout main
git merge --allow-unrelated-histories import/docs -m "chore: import decode-orc-docs history under docs/"
git branch -d import/docs
git remote remove docs-origin
```

After this, `docs/` contains all documentation files with their full commit history visible via `git log -- docs/`.

### 1.2 Verify

```bash
ls docs/          # should show the imported content
git log --oneline -- docs/ | head -10
```

### 1.3 Archive the docs repo

- In `simoninns/decode-orc-docs` → Settings → Archive this repository
- Update the docs repo's README to point to the new location: "Documentation has moved to simoninns/decode-orc under docs/"
- This preserves any inbound links and makes the move visible to the community

---

## Phase 2: Scan and Update Internal `.md` Links

_After importing `docs/`, all `.md` files in this repo that reference the old external docs repo URL or repository must be updated to point to the new canonical location._

### 2.1 Known files with external docs references

A grep of the current repo (`grep -r "decode-orc-docs" --include="*.md"`) reveals the following files that need attention:

| File | Type of reference | Action |
|------|-------------------|--------|
| `README.md` (lines 10, 25, 35–37) | GitHub Pages URLs (`simoninns.github.io/decode-orc-docs/...`) and repo link | Update to new GitHub Pages URL or relative `docs/` paths |
| `encode-tests/README.md` (line 97) | GitHub Pages URL (`simoninns.github.io/decode-orc-docs/encode-orc/...`) | Update to new URL or relative path |

### 2.2 GitHub Pages URL strategy

The existing links point to `https://simoninns.github.io/decode-orc-docs/`. After the merge, GitHub Pages should be configured to publish from the `decode-orc` repo instead. The new URL will be `https://simoninns.github.io/decode-orc/` (or a custom domain if one is configured).

**Decision needed before this phase:** Choose between:
- **Option A — Relative `docs/` paths** for links inside this repo (e.g., `[Installation](docs/decode-orc/installation/linux-flatpak/README.md)`). Works offline and in the GitHub file browser, but not as clean for external links in `README.md`.
- **Option B — New GitHub Pages URL** (`https://simoninns.github.io/decode-orc/...`). Cleaner for `README.md` and external audiences, but requires GitHub Pages to be set up first (Phase 7).
- **Option C — Redirect** — leave `simoninns.github.io/decode-orc-docs` alive pointing to the archived repo while updating internal repo links to the new URL. GitHub Pages for archived repos keeps working until explicitly disabled.

The plan assumes **Option B** for external-facing links in `README.md` and `encode-tests/README.md`, with Option C as a temporary fallback while GitHub Pages is being set up.

### 2.3 Specific changes required

**`README.md`**

```diff
-  > [Click here for the documentation](https://simoninns.github.io/decode-orc-docs/index.html)
+  > [Click here for the documentation](https://simoninns.github.io/decode-orc/index.html)

-  The user documentation is available via [Github Pages](https://simoninns.github.io/decode-orc-docs/index.html)
+  The user documentation is available via [Github Pages](https://simoninns.github.io/decode-orc/index.html)

-  - [Linux Flatpak installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/linux-flatpak/)
-  - [MacOS DMG installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/macos-dmg/)
-  - [Windows MSI installation](https://simoninns.github.io/decode-orc-docs/decode-orc/installation/windows-msi/)
+  - [Linux Flatpak installation](https://simoninns.github.io/decode-orc/decode-orc/installation/linux-flatpak/)
+  - [MacOS DMG installation](https://simoninns.github.io/decode-orc/decode-orc/installation/macos-dmg/)
+  - [Windows MSI installation](https://simoninns.github.io/decode-orc/decode-orc/installation/windows-msi/)
```

**`encode-tests/README.md`**

```diff
-  https://simoninns.github.io/decode-orc-docs/encode-orc/user-guide/project-yaml/
+  https://simoninns.github.io/decode-orc/encode-orc/user-guide/project-yaml/
```

### 2.4 Re-scan after import

Once `docs/` is imported in Phase 1, run the following to catch any additional references that may exist inside the imported docs content itself:

```bash
grep -r "decode-orc-docs" --include="*.md" .
grep -r "simoninns.github.io/decode-orc-docs" --include="*.md" .
```

Any hits inside `docs/` are self-referencing docs links that also need updating to the new GitHub Pages URL.

---

## Phase 3: CODEOWNERS — Route Docs PRs to the Right Reviewer

_CODEOWNERS ensures that a PR touching only `docs/` is automatically assigned to a docs reviewer and does not need a C++ code reviewer to approve it._

### 3.1 Create `.github/CODEOWNERS`

```
# Default owners for everything not matched below
*                   @simoninns

# Documentation — assign to doc maintainer(s); can be a different person or team
docs/**             @simoninns
```

Adjust the GitHub usernames or team handles as appropriate. If a docs-specific maintainer exists, add them as the sole owner of `docs/**` so they receive PR review requests automatically.

### 3.2 How this helps non-technical contributors

- PRs that only modify files under `docs/` are routed exclusively to the docs reviewer
- The contributor never interacts with C++ maintainers unless they choose to comment
- Branch protection can be configured to require review only from the matching CODEOWNERS entry

---

## Phase 4: Documentation PR Template

_A separate PR template pre-fills a docs-specific checklist when a contributor opens a documentation PR, hiding all code-related items._

### 4.1 Create `.github/PULL_REQUEST_TEMPLATE/documentation.md`

GitHub selects a template when the URL contains `template=documentation.md`, or contributors can choose it from the template picker.

```markdown
## Documentation change

Briefly describe what you changed and why.

## Checklist

- [ ] Spelling and grammar checked
- [ ] Links verified (no broken URLs)
- [ ] Screenshots updated if UI changed
- [ ] Content is accurate for the current release

## Related issue (if any)

Closes #
```

The existing `.github/pull_request_template.md` (the default for code PRs) is left unchanged.

### 4.2 Guide contributors to the right template

Add a short note to `docs/CONTRIBUTING.md` (or `docs/README.md`) inside the docs directory itself:

```markdown
## Contributing to the documentation

You can edit any file directly on GitHub — no local setup needed:
1. Browse to the file you want to change
2. Click the pencil ✏ icon (top right of the file view)
3. Make your edit and click **Propose changes**
4. Open a pull request — use the **Documentation** template

Alternatively, press `.` anywhere in the repository on GitHub to open
the browser-based editor for larger changes.
```

---

## Phase 5: Skip CI for Docs-Only Changes

_A docs-only PR should not trigger the C++ build, Nix environment setup, or packaging workflows. This makes CI fast and avoids confusing non-technical contributors with build failures unrelated to their change._

### 5.1 Add path filters to `main.yml`

The main CI/CD workflow currently triggers on every push and PR with no path filter. Add a path filter so the build jobs only run when non-docs files change:

```yaml
on:
  push:
    paths-ignore:
      - 'docs/**'
  pull_request:
    paths-ignore:
      - 'docs/**'
```

### 5.2 Add a lightweight docs-only CI job (optional but recommended)

For docs PRs, provide automated feedback without running the full build. Create `.github/workflows/docs-check.yml`:

```yaml
name: Docs Check

on:
  pull_request:
    paths:
      - 'docs/**'

jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Check for broken Markdown links
        uses: gaurav-nelson/github-action-markdown-link-check@v1
        with:
          use-quiet-mode: 'yes'
          folder-path: 'docs'
```

This gives non-technical contributors meaningful CI feedback (broken links) without triggering the C++ toolchain.

---

## Phase 6: Update copilot-instructions.md

_Tell the AI about `docs/` so it uses it correctly and understands it is part of the normal working tree._

Update `.github/copilot-instructions.md` — specifically the source tree layout table and directory listing — to:

1. Add `docs/` to the repo root layout with a description: `User-facing documentation (merged from decode-orc-docs)`
2. Note that documentation files are plain files under `docs/` committed with the same single-step workflow as any other file
3. Add a short note in the "Where to find things" section pointing to `docs/` for user-facing documentation

No architectural constraints change — `docs/` is not a code module and is not subject to MVP enforcement.

---

## Phase 7: GitHub Repository Settings (UI, no file changes)

_Small configuration changes in the GitHub UI to complete the setup._

- **GitHub Pages:** Configure GitHub Pages on the `decode-orc` repo to publish from `docs/` (or a dedicated branch) so `simoninns.github.io/decode-orc/` serves the documentation. Do this before or alongside Phase 2 link updates.
- **Branch protection on `main`:** Ensure "Require review from Code Owners" is enabled so the CODEOWNERS file is enforced
- **Auto-merge:** Enable auto-merge on the repo so docs PRs that pass the docs-check workflow can be auto-merged by the reviewer with one click
- **Topics / description:** Update the repo description to reflect that documentation now lives here
- **Archive the docs repo:** Archive `simoninns/decode-orc-docs` and update its README to redirect to the new location

---

## Implementation Sequence

| Phase | Description | Where | Depends on |
|-------|-------------|-------|-----------|
| 1 | Import docs history under `docs/` | Local + push | nothing |
| 2 | Scan and update `.md` links/references | This repo | Phase 1 (need imported content to re-scan) |
| 3 | Create `.github/CODEOWNERS` | This repo | Phase 1 |
| 4 | Create docs PR template | This repo | Phase 1 |
| 5 | Add CI path filters + docs-check workflow | This repo | Phase 1 |
| 6 | Update copilot-instructions.md | This repo | Phase 1 |
| 7 | GitHub UI settings + GitHub Pages setup | github.com | Phase 2 (URLs must be ready first) |

Phases 1–6 can all land in a **single PR**. Phase 7 is GitHub UI only and should be done before or immediately after the PR merges.

---

## Files Changed / Created (Summary)

**In this repo (`decode-orc`):**
- `docs/` — new directory tree (imported from `decode-orc-docs` with full history)
- `README.md` — update 5 GitHub Pages URLs from `decode-orc-docs` to `decode-orc`
- `encode-tests/README.md` — update 1 GitHub Pages URL
- `docs/**/*.md` — any self-referencing `decode-orc-docs` links found during Phase 2 re-scan
- `.github/CODEOWNERS` — new
- `.github/PULL_REQUEST_TEMPLATE/documentation.md` — new (existing default template unchanged)
- `.github/workflows/main.yml` — add `paths-ignore: docs/**` filters
- `.github/workflows/docs-check.yml` — new lightweight docs lint workflow
- `.github/copilot-instructions.md` — update source tree layout

**In the docs repo (`decode-orc-docs`) — after archiving:**
- `README.md` — update to redirect to new location (`simoninns/decode-orc`)

**GitHub UI (no file changes):**
- GitHub Pages: configure on `decode-orc` repo
- Branch protection: enable "Require review from Code Owners"
- Optionally enable auto-merge on the repository
- Archive `simoninns/decode-orc-docs`

