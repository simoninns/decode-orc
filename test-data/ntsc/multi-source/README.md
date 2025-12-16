# Multi-Source NTSC Test Files

Multiple NTSC captures of the same mastered content for testing disc stacking and alignment features.

## Organization

Group related captures together in subdirectories:

```
multi-source/
├── movie_a/
│   ├── disc1.tbc              # First pressing, player A
│   ├── disc1.tbc.json
│   ├── disc2.tbc              # Second pressing, player B
│   ├── disc2.tbc.json
│   └── README.md              # Documents differences
└── documentary_b/
    ├── source_a.tbc           # Player skip at field 500
    ├── source_b.tbc           # Clean capture
    ├── source_c.tbc           # Repeated field at 1200
    └── alignment_notes.txt
```

## Purpose

Use these for:
- Testing field fingerprinting and alignment (Section 13 of DESIGN.md)
- Validating stacking algorithms
- Testing MasterFieldID mapping
- Comparing alignment strategies (VBI-based vs content-based)
- Testing confidence scoring and source selection

## Requirements

Each set should include:
- At least 2 NTSC captures of the same mastered content
- Captures should have some variation (different players, different pressings, different disc conditions)
- Ideally include some skips, jumps, or differences between sources to test alignment robustness
- Document known differences between sources in README or notes file
- Include at least one "good" reference capture

## Alignment Challenges

Good test sets should include examples of:
- Player skips (missed fields)
- Repeated fields (stuck tracking)
- Jump discontinuities (skip to different section)
- Different capture start points
- VBI data mismatches (if any)
- NTSC-specific timing variations (~59.94 Hz complications)
- Closed caption continuity issues
