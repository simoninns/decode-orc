# Field and Frame Numbering Conventions (GUI Presentation)

## Scope

All field and frame information presented to the user must follow a single, consistent numbering convention and should use shared helper functions wherever possible.

This logic is **presentation-only**.  
All mapping and conversion code must exist **only in the GUI layer**.

---

## Internal Representation

Internally, everything is **field-based** and **0-indexed**.

- `fieldID` is a monotonically increasing integer:  
  `0, 1, 2, 3, ...`
- `fieldLineIndex` is the 0-indexed line number within a field

There is **no internal concept of a frame**.  
A frame is defined only for presentation purposes.

### Field Geometry

| Field parity (`fieldID % 2`) | Lines | `fieldLineIndex` range |
|-----------------------------|-------|------------------------|
| Even (first field)          | 312   | `0..311`               |
| Odd (second field)          | 313   | `0..312`               |

---

## Conceptual Frame Construction

A presented frame is formed by interleaving two consecutive fields:

- Frame `N` consists of:
  - Even field: `fieldID = 2 × (N − 1)`
  - Odd field:  `fieldID = 2 × (N − 1) + 1`

Interleaving order:

```
even field line 0
odd  field line 0
even field line 1
odd  field line 1
...

```

Because the odd field has one extra line, the final frame line comes from the
odd field:

- Last frame line → odd field line index `312`

---

## Presentation Model (User-Visible)

All presentation numbering is **1-indexed**.

### Frames

- Frames start at **1**
- Each frame has **625 frame lines**
- Frame lines are numbered **1..625**

### Presentation Field Lines

When showing field lines to the user, numbering is **continuous across the two
fields** that make up a frame:

| Field (within frame) | Presentation field lines |
|----------------------|--------------------------|
| First field (even)   | `1..312`                 |
| Second field (odd)   | `313..625`               |

---

## Mapping Displayed in the GUI

### Frame View

Format:
```

Frame <N> line <L> (Field <F> line <FL>) [fieldID <id> – <fieldLineIndex>]

```

Where:
- `Field 1` = first field of the frame (even `fieldID`)
- `Field 2` = second field of the frame (odd `fieldID`)
- `<fieldLineIndex>` is always 0-indexed

#### Examples (Frame 1)

```

Frame 1 line 1   (Field 1 line 1)   [fieldID 0 – 0]
Frame 1 line 2   (Field 2 line 313) [fieldID 1 – 0]
Frame 1 line 623 (Field 1 line 312) [fieldID 0 – 311]
Frame 1 line 624 (Field 2 line 624) [fieldID 1 – 311]
Frame 1 line 625 (Field 2 line 625) [fieldID 1 – 312]

```

---

### Field View

When displaying a field directly:

```

Field 1 line 1   [fieldID 0 – 0]
Field 2 line 313 [fieldID 1 – 0]

```

If fields are shown by `fieldID`, the presentation field number is derived from
parity:

- Even `fieldID` → Field 1
- Odd  `fieldID` → Field 2

---

## GUI Helper Functions

All GUI code must use helper functions that convert presentation values into the
internal representation.

### 1. Frame → Internal Field

**Input**
- `frameNumber` (1-indexed)
- `frameLineNumber` (1-indexed)

**Output**
- `fieldID` (0-indexed)
- `fieldLineIndex` (0-indexed)

---

### 2. Presentation Field → Internal Field

**Input**
- `fieldID`
- `presentationFieldLine`
  - `1..312` for even `fieldID`
  - `313..625` for odd `fieldID`

**Output**
- `fieldID`
- `fieldLineIndex` (0-indexed)

---

## Enforcement Rule

- All non-GUI code must operate exclusively on:
```

fieldID + fieldLineIndex

```
- Frames and presentation numbering must never exist outside the GUI
- All future display modes must be derived from the same helper functions to guarantee consistency