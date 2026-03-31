# Input Formats

encode-orc supports multiple input file formats:

| Format | Extension | Notes |
|--------|-----------|-------|
| YUV422 | `.raw` | Raw uncompressed video data |
| PNG | `.png` | Single frame or sequence |
| QuickTime | `.mov` | Video file format |
| MP4 | `.mp4` | MPEG-4 video format |

All inputs are internally normalized to YUV444P16 (16-bit per component) for maximum precision during encoding.

Note that MOV and YUV422 raw formats support "studio colour" that allows sub-blacks and over-whites required for testcards (for example a PLUGE).  MP4 and PNG are just for including 'normal' pictures and video into the output but are not accurate and should not be used for actual testcards.