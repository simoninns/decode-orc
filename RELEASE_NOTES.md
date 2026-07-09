# Release Notes — since v2.0.1

## Highlights

- **Unified video sink stage** — the separate *Raw video sink* and *FFmpeg video sink* stages have been merged into a single **Video sink** stage, simplifying configuration and the plugin set.
- **Reworked dropout map editor** — significant accuracy and usability improvements to the dropout map editing dialog.
- **1-indexed UI** — all frame and line numbers shown in the UI are now consistently 1-based, while internals remain 0-based.

## Chroma decoder fixes

- Fixed a bug in the **PAL Transform 3D** decoder that caused output corruption.
- Corrected the **NTSC 3D** decoder look-ahead to use the right number of frames.

## Video sink / encoding

- Merged raw and FFmpeg video sinks into one **Video sink** stage.
- Substantially expanded the FFmpeg output backend, including libavfilter graph support (display aspect ratio, video filters, audio gain) — resolves **#208**.
- FFmpeg preset dialog improvements.

## Dropout map editor

- Improved crosshair accuracy and scaling.
- Fixed bugs around dropouts, especially **removing source dropouts via mapping**.
- Further editor refinements and small bug fixes.

## Metadata & compatibility

- Improved handling of **legacy JSON sidecar metadata**.
- Bumped the **CVBS file format** submodule to the latest commit.

## UI / preview

- Harmonised all UI frame numbering to be **1-indexed**.
- Fixed preview dialog playback — resolves **#207**.

## Internals / maintenance

- Cleaned up the plugin mechanism and reduced complexity around the trust model.
- Added an `lru_cache` helper to the stage SDK and minor rendering/stacker/tbc_source tweaks.
- Documentation review and updates.
