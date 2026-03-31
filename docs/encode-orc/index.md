# Introduction

encode-orc is a command-line application that generates test data for [decode-orc](https://github.com/simoninns/decode-orc){target="_blank"}. It produces field-based video files in the TBC (Time Base Corrected) format along with accompanying metadata, simulating the output of RF decoding applications.  It allows the creation of test video and audio that can be used to unit-test decode-orc as well as creating video files to assist with experimentation and learning.

![](./assets/encode-orc_logotype.png)

Encode-orc is also capable of simulating adverse capture conditions such as noise and dropouts in a very controlled manner making it particularly suitable for testing difficult logic such as stacking and dropout correction.

## Key Features

- **YAML-based configuration** - Define video projects declaratively
- **Multiple input formats** - RAW (YUV422), PNG, MOV, MP4
- **PAL and NTSC support** - Both 576i and 480i video systems
- **Flexible output modes** - Composite and separate Y/C (S-Video) encoding 
- **Metadata generation** - SQLite database and embedded metadata
