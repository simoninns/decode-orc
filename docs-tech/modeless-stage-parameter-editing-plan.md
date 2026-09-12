# Modeless Stage Parameter Editing — Implementation Plan

## Purpose

Issue [#299](https://github.com/decode-orc/decode-orc/issues/299): the stage
parameter dialogue is application-modal, so while it is open nothing else in
the application can be touched — not the preview, not the graph, not a second
stage's parameters. The reader asks for two things: parameter windows that can
be opened alongside each other and alongside the rest of the application, and a
way to tell at a glance which node each one belongs to.

This matters more since the live-update work of issue #295. Live update exists
so a value judged by eye — black level, chroma gain — can be adjusted while
watching the result, but a modal editor puts the preview behind a window the
user cannot leave, and comparing two stages' settings means closing one to see
the other. The feature is only half usable while the dialogue holds the
application.

## Current behaviour

There is exactly one parameter-editing path. `MainWindow::onEditParameters()`
(`orc/gui/mainwindow.cpp`) builds a stack-allocated `StageParameterDialog`,
wires `update_requested` / `live_update_requested` to a local lambda, and
finishes with `dialog.exec()`. Every stage reaches the editor through this
function, so modality is one property in one place rather than a per-stage
trait: no stage needs changing, and no stage can opt out.

The application already knows how to run modeless per-node windows. The
analysis and catalogue viewers keep an `unordered_map<NodeID, Dialog*>`, set
`WA_DeleteOnClose`, and erase their map entry from the dialogue's `destroyed`
signal; the dropout editor additionally gives itself `Qt::Window` flags so it
is an independent window rather than a panel floating over the main window.
This plan follows that pattern rather than inventing a second one.

## Design decisions

**One editor per node, not per invocation.** The issue asks for many parameter
windows; it does not ask for two windows onto the same node's parameters, which
would present two edit buffers over one set of values with no rule for which
wins. A second **Edit Parameters…** on a node that already has an editor open
raises that editor.

**The editor is not a result viewer.** `updatePreviewRenderer()` calls
`closeResultViewers()`, on the grounds that a rebuilt DAG invalidates every
cached analysis. The apply path calls `updatePreviewRenderer()`, so an editor
registered as a result viewer would close itself the moment the user pressed
**Update**, or on the first live-update tick. Parameter editors join
`closeAllDialogs()` (project switch) and stay out of `closeResultViewers()`.

**Open windows are kept truthful, not left to rot.** A modal dialogue cannot go
stale: nothing can change underneath it. A modeless one can, in three ways —
the node is deleted, the graph around it is rewired, or its values are changed
from elsewhere (another editor, the validation-failure reset path, a project
reload). Each is handled explicitly below. This is the bulk of the work, and it
is why the context that `onEditParameters()` computes inline has to become a
function that can be called more than once.

**Cancel is renamed, not redefined.** With live update ticked the edits are
already applied, so a button labelled Cancel is a lie — and it is a lie the
user has much longer to notice once the window can sit open beside the preview.
Making Cancel revert to the opening values was considered and rejected: it
contradicts the usual meaning of Apply-then-Cancel, and it would silently throw
away a good setting the user had already accepted by eye. Instead the button
says **Close** whenever live update is ticked, and **Cancel** when it is not.

## Why a single phase

The three staleness problems share one mechanism: a rebuildable context plus a
way to push it into an open dialogue. Splitting them would mean shipping a
window that can be left showing a deleted node's parameters, which is a worse
state than the modal editor it replaces. The tasks below are ordered so the
work can still be landed as separate commits — tasks 1 and 2 are pure
refactoring with no behaviour change, and are the natural cut point if the
change wants splitting on review.

## Task 1 — Extract the parameter context as a pure function

New `orc/gui/stage_parameter_context.h/.cpp`. `onEditParameters()` currently
spends around 200 lines deriving what the dialogue should show from the graph
around the node: the audio channel-pair dropdown narrowed to the pairs the
upstream node actually carries (`audio_channel_map`, `audio_align`,
`AudioSink`, `tbc_sink`), the reset-to-metadata values read from the stage
input rather than the `video_params` output, the `source_join` connected-input
notice, and the channel-pair notices appended to the stage description. All of
it is derivation, none of it needs a presenter once the raw material is to
hand.

Split it so the presenter calls stay in `MainWindow` and the derivation becomes
a free function over plain inputs:

```cpp
struct StageParameterContextInputs {
  std::string stage_name;
  std::vector<orc::ParameterDescriptor> descriptors;
  std::map<std::string, orc::ParameterValue> current_values;
  std::string stage_description;
  // Only the material the stage in question needs; empty otherwise.
  std::vector<orc::gui::ConnectedInputNode> connected_inputs;
  std::optional<std::vector<std::string>> input_audio_pair_names;
  std::optional<orc::SourceParameters> input_video_parameters;
};

struct StageParameterContext {
  std::vector<orc::ParameterDescriptor> descriptors;
  std::map<std::string, orc::ParameterValue> current_values;
  std::string stage_description;
  std::optional<std::map<std::string, orc::ParameterValue>> reset_values;
};

StageParameterContext buildStageParameterContext(StageParameterContextInputs);
```

`sourceParametersToVideoParamsStageValues()` and
`applyMetadataFallbackValues()`, today file-local statics in `mainwindow.cpp`,
move into this translation unit.

`MainWindow` gains a private `gatherStageParameterContext(NodeID)` that does the
presenter work — node lookup, `getStageParameters()`, `getNodeParameters()`,
the throwaway `RenderPresenter` for pair names and video parameters, the edge
walk for the input node — and hands the result to the free function. It is
called on open and again on refresh (task 4).

**Acceptance criteria**
- No behaviour change: the dialogue opens with the same descriptors, values,
  description and reset values for every stage as before.
- `buildStageParameterContext()` touches no presenter, no Qt widget and no
  filesystem; it is a Tier 1 `gui-logic` unit.
- Unit tests in `orc-tests/gui/unit/stage_parameter_context_test.cpp` (labels
  `unit`, `gui`, `gui-logic`) covering: pair-count narrowing for each of the
  four audio stages, `target_pair` keeping its `new` entry, the `tbc_sink`
  sidecar notice versus the stronger notice for the rest, the `source_join`
  input listing, `video_params` reset values and metadata fallback, and a
  stage needing none of these passing through untouched.
- `ctest -R MVPArchitectureCheck` clean.

## Task 2 — Let the dialogue be re-seeded and re-identified

`StageParameterDialog` gains:

- `void refresh_context(const StageParameterContext&)` — replaces descriptors,
  combo entries, the description text and the reset values, and rewrites the
  form's values, then runs `update_dependencies()`. The form is rebuilt only
  when the descriptors have actually changed shape, compared by
  `descriptorsMatch()` over the fields a row is built from; a refresh that
  changes no descriptor — the common one, since it happens after every apply —
  leaves every widget in place, and with it the caret and any part-typed text.
  A descriptor change rebuilds the whole form rather than the affected rows:
  it happens only when the graph is rewired, and taking a `QFormLayout` apart
  row by row to save a caret in that case buys nothing worth the risk.
- `void refresh_values(const std::map<std::string, orc::ParameterValue>&)` —
  the values-only case, reusing `set_widget_value()`.
- `bool has_unapplied_edits() const` — true when the form differs from the last
  applied values (`last_live_values_` already tracks this for the live path;
  generalise it to cover Update and OK).
- `void set_node_identity(const QString& label, const QString& node_id)` —
  sets the window title to `"<Display name> Parameters — <label> (<node id>)"`,
  matching the title `SNRAnalysisDialog` already builds, so the issue's second
  request is met and a renamed stage can have its title corrected in place.

A refresh must not discard work in progress. Where `has_unapplied_edits()` is
true, `refresh_values()` leaves the form alone and the caller is told, so the
decision about what to do with a conflicting external change is made once, in
`MainWindow` (task 4), rather than differently at each call site.

The **Cancel**/**Close** text swap lives in `on_live_update_toggled()`.

**Acceptance criteria**
- Round trip: build, `refresh_context()` with narrowed combos, `get_values()`
  returns the new values; a parameter absent from the new descriptors is gone
  from the form and one added appears.
- A refresh with unapplied edits present leaves the form's values untouched.
- Live update ticked shows **Close**; unticking restores **Cancel**.
- Tier 3 `gui-widget` tests added to
  `orc-tests/gui/unit/stage_parameter_dialog_test.cpp`, offscreen.

## Task 3 — Open the editor modeless, one per node

`MainWindow` gains
`std::unordered_map<orc::NodeID, StageParameterDialog*> parameter_dialogs_`
beside the existing per-node dialogue maps.

`onEditParameters()` becomes: look the node up in the map; if present,
`show()`, `raise()`, `activateWindow()` and return. Otherwise build the context
(task 1), construct the dialogue on the heap parented to the main window with
`WA_DeleteOnClose`, give it `Qt::Window | Qt::WindowMinimizeButtonHint |
Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint` so it is an
independent window with its own taskbar entry, set its node identity, register
it, connect `destroyed` to erase the map entry, and `show()`.

The apply lambda becomes a member,
`applyStageParameters(NodeID node_id, QPointer<StageParameterDialog> dialog, bool live)`.
This is not cosmetic: the present lambda captures `dialog` and `node_id` by
reference and is connected to signals that will outlive the enclosing function
the moment `exec()` goes away. `update_requested`, `live_update_requested` and
`accepted` all route through it.

Two further corrections in that path:

- The validation-failure `QMessageBox::critical` is parented to the dialogue
  that raised it, not to the main window, so it blocks one editor rather than
  all of them.
- The recovery branch that clears a node's parameters after a rejected apply
  keeps its current behaviour but is logged with the node id, since with
  several editors open the status bar no longer says which stage a message came
  from.

Placement: a new editor is offset from the last one opened so that two windows
do not land exactly on top of each other, in the manner of a cascading MDI
child, bounded to the available screen.

`closeAllDialogs()` closes and clears `parameter_dialogs_` using the
collect-then-close sequence the other maps use, the comment there explaining
why. `closeResultViewers()` does **not** — see Design decisions.

**Acceptance criteria**
- With an editor open, the main window, preview and a second stage's editor are
  all interactive.
- A second **Edit Parameters…** on the same node raises the existing window and
  does not create a second one, preserving any part-finished edit.
- Closing an editor removes its map entry; closing the project closes every
  editor; pressing **Update** closes none.
- The window title names the stage, its label and its node id.
- Tier 3 offscreen coverage of what the dialogue itself carries: the title, the
  refresh paths and the button text.

The registry lifecycle is `MainWindow`'s own, and nothing in
`orc-tests/gui/unit` constructs a `MainWindow` — it needs a project, a running
coordinator and the plugin set, which is what keeps these tests at the
presenter boundary. The per-node analysis and catalogue maps this follows are
uncovered for the same reason. Covered by the manual check below instead;
building a seam for it is a separate piece of work from this one.

## Task 4 — Keep open editors truthful

Three sources of staleness, all handled from `MainWindow`.

**Deleted node.** `OrcGraphModel::deleteNode()` erases its id mapping before
emitting `nodeDeleted`, so a handler on that signal cannot map the payload back
to an `orc::NodeID`. Instead `onDAGModified()` sweeps `parameter_dialogs_` and
closes any entry whose node is no longer in `presenter()->getNodes()`. Left
open, such an editor would apply to a missing node and take the drastic
"parameters have been reset" branch against nothing.

**Rewired graph.** The same sweep re-runs `gatherStageParameterContext()` for
each surviving editor and calls `refresh_context()`. This is what keeps the
channel-pair dropdown, the `source_join` input listing and the `video_params`
metadata reset values honest when the user reconnects an input while the editor
is open.

**Values changed elsewhere.** After a successful apply, every *other* open
editor is refreshed from the presenter; the originating editor is skipped, or
it would have the user's in-progress typing overwritten by the values that
typing just produced. An editor holding unapplied edits declines the refresh
(task 2) and is marked in its window title with a trailing `*`, so a
conflicting external change is visible rather than silently lost.

A rename also reaches the open editor: `set_node_identity()` is called from the
sweep, so **Rename Stage…** corrects the title rather than leaving the old
label in the window list.

**Acceptance criteria**
- Deleting a node with its editor open closes that editor and leaves other
  editors alone.
- Reconnecting an audio stage's input to a source with a different pair count
  updates the open editor's dropdown.
- Applying from one editor refreshes the values shown in the others; an editor
  with unapplied edits keeps them and shows the modified marker.
- Renaming a stage updates its open editor's title.
- The dialogue half of each — what `refresh_context()`, `refresh_values()` and
  `set_node_identity()` do when called — is covered offscreen at Tier 3; the
  `MainWindow` half is the manual check, for the reason given under task 3.

## Task 5 — Documentation

`docs/gui-user-guide/dialogues/main.md`, "Editing Stage Parameters": say that
the parameter window is now an independent window, that one can be opened per
stage and left open while the graph and preview are used, that the title names
the stage it belongs to, and that with live update ticked the button reads
Close because the values are already applied. The existing OK / Update / live
update description stays.

No `instructions.md` changes: no stage's parameters, tools or behaviour change,
only the window the host presents them in. No `plugin_ux_capabilities.yaml`
entry for the same reason — no capability is added or removed.

Delete this plan document once the work has landed.

## Deliberately out of scope

- **Coalescing applies across editors.** Each apply rebuilds the DAG and
  re-renders the preview, and several editors with live update ticked will do
  so in turn. A main-window-level settle timer would fix it, but whether it is
  needed is a question for measurement on a real project rather than for
  speculation now.
- **The parameter-reset recovery branch.** Clearing a node's parameters when an
  apply is rejected is drastic and predates this work. It stays as it is here;
  changing it is its own issue.
- **Docking the editors.** The issue asks for windows that do not hold the
  application, not for a parameter panel in the main window.

## Validation

Per AGENTS.md §4.6, GUI behaviour change:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_UNIT_TESTS=ON -DBUILD_GUI_TESTS=ON
cmake --build build -j
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L gui --output-on-failure
ctest --test-dir build -R MVPArchitectureCheck --output-on-failure
```

Manual check, which the offscreen tests cannot stand in for: open editors on
two stages of a live project, tick live update on one, and confirm the preview
follows the slider while the graph, the preview controls and the second editor
all stay usable.
