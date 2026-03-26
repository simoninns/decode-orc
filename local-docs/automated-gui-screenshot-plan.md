# Plan: Automated `orc-gui` Screenshot Generation

## Goal

Automatically generate deterministic PNG screenshots of key `orc-gui` windows, dialogs, and modals so documentation and UI regression checks can be updated consistently with minimal manual effort.

This plan focuses on a practical staged rollout:

- Start with a small curated set of high-value UI surfaces
- Use a deterministic test project so screenshots contain meaningful real-looking data
- Run headless on Linux CI and locally
- Avoid brittle pixel comparisons at first, then optionally add visual diff gates

---

## Success Criteria

1. A single command generates all planned screenshots as PNG files.
2. Screenshots include realistic data/state (not empty default windows).
3. Runs in CI headless (`QT_QPA_PLATFORM=offscreen` or `xvfb`) with stable output.
4. Output naming is deterministic and traceable to screen/state IDs.
5. Failures clearly identify which capture target failed and why.
6. Workflow does not break existing `unit` test constraints.

---

## Non-Goals (Initial Phase)

- Perfect cross-platform pixel-identical rendering on Linux/macOS/Windows.
- Capturing every transient animation frame.
- Native OS dialogs (file pickers, platform alert sheets) in first delivery.

---

## Key Design Decisions

## 1) Introduce a dedicated screenshot harness (not unit tests)

Because project unit tests forbid filesystem and external resources, screenshot automation should live in a separate test/tool layer (for example a `gui-snapshots` label or standalone capture executable). This allows controlled loading of fixture data while keeping `unit` suites pure.

## 2) Use deterministic fixture projects for meaningful UI state

Create one or more curated fixture `.orcprj` projects specifically for screenshots, with:

- Representative pipeline graph content
- Parameter values set to non-default interesting states
- Metadata sufficient to populate summary/detail panes
- Stable sample references (or mocked/embedded substitutes) to avoid flaky runtime dependencies

## 3) Capture at presenter boundary where possible

Prefer constructing windows/dialogs with presenter-boundary test doubles and stable view-model data for maximum determinism; only load full fixture project files where needed to show end-to-end state.

## 4) Separate capture from strict visual diff gating

Phase 1 should produce reproducible images and artifacts. Pixel diff pass/fail can be added later once rendering stability is validated.

---

## Target Coverage (Initial)

Minimum recommended first set:

1. Main application window (project loaded, graph visible)
2. Project settings dialog
3. Stage parameter dialog (one source, one transform, one sink example)
4. Render/output dialog or panel
5. At least one confirmation modal and one warning/error modal

Expansion set:

1. Additional stage editors
2. Preview panels in multiple modes
3. Live tweak panels and metadata dialogs
4. Wizard-style flows (first step only at initial rollout)

---

## Requirement: Screenshot Fixture Project(s)

A dedicated screenshot project is required so generated images show meaningful content.

## Fixture requirements

1. Deterministic and version-controlled under repo test assets (for example under `test-projects/` with a screenshot-specific naming convention).
2. Includes enough nodes/parameters to exercise key GUI panels.
3. Uses stable values and labels intended for docs (human-readable, non-random).
4. Avoids machine-specific paths; supports path remapping or test-mode indirection.
5. Contains at least one intentionally configured non-default parameter set so dialogs are informative.

## Optional fixture tiers

1. `minimal` fixture: fast smoke coverage
2. `docs-rich` fixture: visually informative screenshots for user docs
3. `edge-case` fixture: optional state coverage for complex modals

---

## Technical Approach

## A) Screenshot target registry

Define a central list of screenshot targets with IDs and capture callbacks, for example:

- `main-window.project-loaded`
- `dialog.project-settings.basic`
- `dialog.stage-params.source-ld`
- `modal.warning.unsaved-changes`

Each target declares:

1. Setup routine (load fixture / inject presenter state)
2. Widget locator / creator
3. Pre-capture stabilization steps
4. Output filename

## B) Rendering stabilization steps

Before capture:

1. Force deterministic window size and device pixel ratio assumptions.
2. Wait for event loop idle and queued signals completion.
3. Disable animations/transitions in test mode where practical.
4. Apply stable style/theme and known font fallback.

## C) Capture mechanism

- Use Qt capture APIs (`QWidget::grab()` or equivalent per widget/view type).
- Save to PNG with deterministic file paths and names.
- Emit per-target logs for traceability.

## D) Headless execution

Primary CI mode:

- `QT_QPA_PLATFORM=offscreen` for Qt widget captures.

Fallback mode if needed:

- `xvfb-run` wrapper for environments where offscreen is incomplete.

---

## CI/CD Plan

## Job behavior

1. Add a dedicated screenshot workflow/job (separate from normal unit CI).
2. Build with GUI tests/tools enabled.
3. Execute screenshot harness.
4. Upload PNG artifacts for PR inspection.
5. Optionally post artifact links in PR summary.

## Trigger policy

Initial recommendation:

1. Run on demand (`workflow_dispatch`) and on PRs touching `orc/gui/**`, `orc/presenters/**`, `orc/view-types/**`, and screenshot fixtures.
2. Keep non-blocking initially to gather stability data.

Later:

1. Make blocking for selected critical surfaces once flake rate is low.

---

## Optional Visual Regression Phase

After stable capture pipeline:

1. Establish golden baseline images in a versioned location.
2. Add diff tool with thresholding/tolerance.
3. Fail only on meaningful changes above threshold.
4. Provide diff overlays as CI artifacts.

Important: perform baselining per platform or pin to one canonical capture platform (recommended Linux) to avoid cross-platform rendering drift.

---

## Architecture and Testing Constraints

1. Keep MVP boundaries intact: no direct `orc/gui` to `orc/core` shortcuts beyond existing architecture.
2. Do not contaminate `unit` labels with filesystem-dependent screenshot tests.
3. Place screenshot automation under clearly named non-unit labels/targets.
4. Preserve existing GUI unit tests as fast behavioral checks; screenshot suite is additive.

---

## Risks and Mitigations

1. Flaky rendering timing:
   Mitigation: explicit idle waits, retries limited to capture setup, disable animations.
2. Font/theme differences across runners:
   Mitigation: pin runner image, install explicit font package, force style where possible.
3. Native dialogs not capturable:
   Mitigation: use non-native dialog mode in test harness.
4. Long runtime if capturing too many surfaces:
   Mitigation: tiered target sets (`smoke` vs `full`).

---

## Implementation Phases

## Phase 1: Foundation

1. Add screenshot harness executable/test target.
2. Define screenshot target registry and naming convention.
3. Implement capture for 2 to 3 core windows/dialogs.
4. Add one deterministic fixture project.

Deliverable: local command generates first PNG set reliably.

## Phase 2: Coverage Expansion

1. Add modal/dialog capture support and setup helpers.
2. Expand fixture content for richer states.
3. Add at least 8 to 12 total capture targets.

Deliverable: broad docs-ready screenshot artifact bundle.

## Phase 3: CI Integration

1. Add CI workflow/job with headless capture.
2. Upload artifacts and summary.
3. Monitor flake rate and stabilize environment.

Deliverable: repeatable PR artifacts for UI review.

## Phase 4: Visual Regression (Optional)

1. Baseline goldens.
2. Add threshold-based diffs.
3. Define policy for approved UI changes.

Deliverable: automated UI regression signal with manageable noise.

---

## Proposed Command UX

Examples (final names TBD):

```bash
# Capture smoke set
ctest --test-dir build -L gui-screenshot-smoke --output-on-failure

# Capture full set
ctest --test-dir build -L gui-screenshot --output-on-failure
```

Or standalone executable style:

```bash
./build/bin/orc-gui-screenshot-capture --fixture screenshot-docs-rich --set smoke --out build/artifacts/gui-snapshots
```

---

## Definition of Done

1. Screenshot harness exists with deterministic output.
2. At least one dedicated fixture project is committed and documented.
3. Main window + key dialogs/modals are captured with meaningful data.
4. CI can run capture headlessly and publish artifacts.
5. Developer docs explain how to regenerate and review screenshots.
6. No impact to existing MVP boundary checks or unit-test purity rules.

---

## Open Decisions

1. Should screenshot generation live as CTest labels or standalone executable (or both)?
2. Canonical capture platform: Linux-only or multi-platform?
3. Where should approved screenshots live for docs consumption (`docs/assets/...` vs CI artifact only)?
4. Should first rollout be non-blocking in CI until flake rate is proven low?

---

## Practical Recommendation

Start with Linux-only headless capture, one screenshot fixture project, and a non-blocking CI artifact workflow. Once stable, expand coverage and consider introducing golden diff gating for a small critical subset.