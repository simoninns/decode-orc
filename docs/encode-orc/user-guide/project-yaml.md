# YAML Project Configuration

encode-orc uses YAML files to define encoding projects. This document describes all available configuration options.

See [Installation and Asset Setup](installation.md) for information on how to install encode-orc and configure environment variables.

---

## Path Resolution

**All file paths in the YAML configuration are resolved relative to the YAML file's directory, not the current working directory.** This makes projects portable and allows you to run `encode-orc` from any location.

### Path Resolution Rules

1. **Absolute paths** are used unchanged
   ```yaml
   file: "/absolute/path/to/file.raw"
   ```

2. **Relative paths** are resolved relative to the directory containing the YAML file
   ```yaml
   # If YAML is at: /home/user/projects/my-project/project.yaml
   # This resolves to: /home/user/projects/my-project/testcard-images/bars.raw
   file: "testcard-images/bars.raw"
   ```

3. **${PROJECT_ROOT} variable** expands to the YAML file's directory
   ```yaml
   # Explicitly reference the project root
   file: "${PROJECT_ROOT}/resources/image.png"
   ```

4. **${ENCODE_ORC_ASSETS} variable** expands to the installed assets directory
   ```yaml
   # Access system-installed testcard images (via Nix installation)
   file: "${ENCODE_ORC_ASSETS}/pal-raw/625_50_75_BARS.raw"
   ```

5. **${ENCODE_ORC_OUTPUT_ROOT} variable** expands to the output root directory
   ```yaml
   # Control output location via environment variable
   filename: "${ENCODE_ORC_OUTPUT_ROOT}/video"
   ```

### Examples

```yaml
# Project structure:
# /home/user/projects/
#   └── my-project/
#       ├── project.yaml
#       ├── output/
#       └── resources/
#           └── testcard.raw

# From anywhere on the system, you can run:
# $ encode-orc /home/user/projects/my-project/project.yaml
# or
# $ cd /tmp && encode-orc ~/projects/my-project/project.yaml

output:
  filename: "output/video"  # Resolves to: /home/user/projects/my-project/output/video

sections:
  - name: "Test"
    source:
      file: "resources/testcard.raw"  # Resolves to: /home/user/projects/my-project/resources/testcard.raw
```

---

## Environment Variables

`encode-orc` supports environment variables to control asset and output locations in your YAML projects. See [Installation and Asset Setup](installation.md) for how to configure these variables based on your installation method.

### ENCODE_ORC_ASSETS

Controls the location of installed testcard images and assets. Use this variable in your YAML to reference system-installed or custom asset locations.

**Usage in YAML:**
```yaml
sections:
  - name: "Content"
    source:
      file: "${ENCODE_ORC_ASSETS}/pal-raw/625_50_75_BARS.raw"
```

**Variable expansion:**
- If the environment variable is set, it expands to that path
- If not set, defaults to `../assets` relative to the YAML file's directory

This allows the same YAML project to work with:
- Nix system-wide installations (`/nix/store/.../share/encode-orc/assets`)
- Local development setups (`./assets`)
- Custom asset locations

###  ENCODE_ORC_OUTPUT_ROOT

Controls the root directory where output files are written. Use this variable to specify a custom output location separate from your YAML project.

**Usage in YAML:**
```yaml
output:
  filename: "${ENCODE_ORC_OUTPUT_ROOT}/my-video"
  format: "pal-composite"
```

**Variable expansion:**
- If the environment variable is set, it expands to that path
- If not set, defaults based on the YAML file's location:
  - `test-projects/` directory → `test-output/`
  - `ggv-tests/` directory → `ggv-output/`
  - Other locations → `output/` (sibling of parent directory)

This makes it easy to redirect all output to a specific location without modifying YAML files.

---

## Quick Example

```yaml
name: "My Project"
description: "A simple PAL encoding example"

output:
  filename: "output/my-video"
  format: "pal-composite"

pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true
  metadata:
    generators:
      - type: "color-burst"
        enabled: true

sections:
  - name: "Content"
    duration: 10
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
```

---

## Top-Level Fields

### `name` (required)
**Type:** String

Project name for identification and logging.

```yaml
name: "PAL Test Pattern"
```

### `description` (required)
**Type:** String

Human-readable description of the project purpose.

```yaml
description: "PAL composite output with test pattern for decoder validation"
```

### `output` (required)
**Type:** Object

Defines output file configuration. See [Output Configuration](#output-configuration).

### `pipeline` (required)
**Type:** Object

Configures the encoding pipeline stages. See [Pipeline Configuration](#pipeline-configuration).

### `sections` (required)
**Type:** Array

List of video sections to encode. See [Sections Configuration](#sections-configuration).

### `laserdisc` (optional)
**Type:** Object

LaserDisc-specific metadata configuration.

```yaml
laserdisc:
  mode: "cav"  # or "clv"
```

**Fields:**
- `mode`: Either `"cav"` (Constant Angular Velocity with picture numbers) or `"clv"` (Constant Linear Velocity with timecode)

---

## Output Configuration

The `output` section defines how the encoded video is written to disk.

### `filename` (required)
**Type:** String

Output file path (without extension). Extensions are added automatically based on format.

```yaml
output:
  filename: "output/my-video"
```

Results in:
- Composite: `my-video.tbc` + `my-video.tbc.json`
- Y/C: `my-video.tbcy`, `my-video.tbcc` + `my-video.tbc.json`

### `format` (required)
**Type:** String  
**Values:** `"pal-composite"` | `"ntsc-composite"` | `"pal-yc"` | `"ntsc-yc"`

Output format and video system.

**Composite formats:**
- `"pal-composite"` - PAL composite video (625 lines, 50 Hz)
- `"ntsc-composite"` - NTSC composite video (525 lines, 59.94 Hz)

**Y/C (S-Video) formats:**
- `"pal-yc"` - PAL with separate luma/chroma channels
- `"ntsc-yc"` - NTSC with separate luma/chroma channels

### `mode` (optional)
**Type:** String  
**Default:** `"combined"`  
**Values:** `"combined"` | `"separate-yc"`

For Y/C formats, controls file output mode:
- `"combined"` - Single interleaved file
- `"separate-yc"` - Separate `.tbcy` (luma) and `.tbcc` (chroma) files

### `writer` (optional)
**Type:** String  
**Default:** `"tbc"`  
**Values:** `"tbc"` | `"standard"`

Output writer type:
- `"tbc"` - TBC format (8-bit per sample, field-based)
- `"standard"` - Standard writer (alternative format)

### `metadata_decoder` (optional)
**Type:** String  
**Default:** `"encode-orc"`

Decoder identification string written to SQLite metadata.

### `sound_format` (optional)
**Type:** String  
**Values:** `"pcm"` | `"wav"`

Audio output format. Only required if sections include `sound` configuration.

```yaml
output:
  filename: "output/video-with-audio"
  format: "pal-composite"
  sound_format: "pcm"
```

### `video_levels` (optional)
**Type:** Object

Override default video signal levels (advanced).

```yaml
output:
  video_levels:
    blanking_16b_ire: 4096    # 16-bit blanking level
    black_16b_ire: 7168       # 16-bit black level
    white_16b_ire: 52428      # 16-bit white level
```

**Fields:**
- `blanking_16b_ire`: Blanking level (0-65535)
- `black_16b_ire`: Black level (0-65535)
- `white_16b_ire`: White/peak level (0-65535)

---

## Pipeline Configuration

The `pipeline` section configures encoding stages and metadata generation.

### `preprocessing` (optional)
**Type:** Object

Configures input preprocessing and filtering.

#### `preprocessing.filters`

Control chroma and luma filtering.

```yaml
pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true   # Enable chroma low-pass filter
      luma:
        enabled: false  # Disable luma filtering
```

**Chroma Filter:**
- **Default:** Enabled
- **Purpose:** Prevents chroma artifacts during subcarrier modulation
- **Filter type:** Automatic based on video system
  - PAL: 1.3 MHz Gaussian filter (13 taps)
  - NTSC: 1.3 MHz filter (9 taps) or narrowband Q filter (23 taps)

**Luma Filter:**
- **Default:** Disabled
- **Purpose:** Smooths luma transitions
- **When to use:** For stylized looks or specific test scenarios

### `metadata` (required)
**Type:** Object

Configures metadata generators that embed information into the video signal.

#### `metadata.generators`

Array of generator configurations. Each generator embeds specific metadata into VBI lines or active video.

```yaml
pipeline:
  metadata:
    generators:
      - type: "color-burst"
        enabled: true
      
      - type: "biphase-vbi"
        enabled: true
        lines: [16, 17, 18, 328, 329, 330]
        format: "picture-number"
      
      - type: "vitc"
        enabled: true
        lines: [19, 331]
        start_frame_offset: 0
```

### Generator Types

#### `color-burst` Generator

Adds color burst reference signal for color decoders.

```yaml
- type: "color-burst"
  enabled: true
```

**Required for:** All color video encoding  
**No additional configuration needed**

---

#### `biphase-vbi` Generator

Encodes LaserDisc metadata (picture numbers or timecode) using biphase modulation.

```yaml
- type: "biphase-vbi"
  enabled: true
  lines: [16, 17, 18, 328, 329, 330]  # Field 1 and Field 2 VBI lines
  format: "picture-number"              # or "timecode"
```

**Fields:**
- `enabled`: Enable/disable generator
- `lines`: Array of absolute line numbers (0-indexed) to encode VBI data
  - PAL: Lines 16-18 (field 1), 328-330 (field 2)
  - NTSC: Lines 16-18 (field 1), 278-280 (field 2)
- `format`: Data format
  - `"picture-number"` - CAV frame numbering
  - `"timecode"` - CLV timecode (HH:MM:SS.FF)

**Used with:** LaserDisc encoding projects  
**See also:** Section-level `biphase-vbi` configuration

---

#### `vitc` Generator

Vertical Interval Timecode - embeds frame numbers and timecode in VBI.

```yaml
- type: "vitc"
  enabled: true
  lines: [19, 331]           # VBI lines for VITC
  start_frame_offset: 0      # Starting frame number
```

**Fields:**
- `enabled`: Enable/disable generator
- `lines`: Array of absolute line numbers for VITC encoding
- `start_frame_offset`: Starting frame number for timecode calculation

**Common line numbers:**
- PAL: Lines 19, 20, 331, 332
- NTSC: Lines 14, 19, 276, 281

---

#### `vits-pal` Generator

PAL Vertical Interval Test Signals - embeds standard test patterns in VBI.

```yaml
- type: "vits-pal"
  enabled: true
  signals:
    - line: 13
      signal: "multiburst"
    - line: 19
      signal: "uk-national"
    - line: 325
      signal: "itu-combination"
    - line: 331
      signal: "itu-composite"
```

**Fields:**
- `enabled`: Enable/disable generator
- `signals`: Array of signal configurations
  - `line`: Absolute line number (1-625 for PAL)
  - `signal`: Test signal type

**PAL Signal Types:**
- `"multiburst"` - Multiple frequency bursts for bandwidth testing
- `"uk-national"` - UK national standard test signal
- `"itu-combination"` - ITU combination test signal
- `"itu-composite"` - ITU composite test signal

**Use case:** Decoder testing and calibration

---

#### `vits-ntsc` Generator

NTSC Vertical Interval Test Signals.

```yaml
- type: "vits-ntsc"
  enabled: true
  signals:
    - line: 13
      signal: "ntc7-composite"
    - line: 19
      signal: "vir"
    - line: 275
      signal: "ntc7-combination"
    - line: 281
      signal: "vir"
```

**Fields:**
- `enabled`: Enable/disable generator
- `signals`: Array of signal configurations
  - `line`: Absolute line number (1-525 for NTSC)
  - `signal`: Test signal type

**NTSC Signal Types:**
- `"ntc7-composite"` - NTC-7 composite test signal
- `"ntc7-combination"` - NTC-7 combination test signal
- `"vir"` - Vertical Interval Reference (VIR) signal for color stability
- `"multiburst"` - Frequency burst pattern

**Use case:** NTSC decoder testing and color reference

---

### `effects` (optional)
**Type:** Object

Simulates analog artifacts and signal degradation.

```yaml
pipeline:
  effects:
    - type: "noise"
      enabled: true
      snr_db: 42.0
      seed: 42
    
    - type: "dropout"
      enabled: true
      density: 0.001
      multi_field_probability: 0.30
      single_field_probability: 0.70
      seed: 123
```

### Effect Types

#### `noise` Effect

Adds Gaussian noise to simulate tape hiss or RF noise.

```yaml
- type: "noise"
  enabled: true
  snr_db: 42.0           # Signal-to-noise ratio in dB
  seed: 42               # Random seed for reproducibility
```

**Fields:**
- `enabled`: Enable/disable effect
- `snr_db`: Signal-to-noise ratio in decibels
  - Higher values = less noise (cleaner signal)
  - Typical ranges: 35-50 dB (good quality), 18-30 dB (degraded)
- `noise_level_db` (alternative): Direct noise level in dB
- `seed`: Random seed (optional, for reproducible results)

**Use case:** Simulating tape wear, RF interference, or playback quality

---

#### `dropout` Effect

Simulates missing data from tape damage, scratches, or read errors.

```yaml
- type: "dropout"
  enabled: true
  density: 0.001                    # Overall dropout density
  multi_field_probability: 0.30     # Probability of multi-field dropouts
  single_field_probability: 0.70    # Probability of single-field dropouts
  seed: 42
```

**Fields:**
- `enabled`: Enable/disable effect
- `density`: Fraction of video samples affected (0.0-1.0)
  - 0.001 = ~0.1% dropout rate
  - Typical: 0.0001-0.01 for light to moderate damage
- `multi_field_probability`: Probability (0.0-1.0) of vertical scratches affecting multiple fields
- `single_field_probability`: Probability (0.0-1.0) of single-field dropouts (disc degradation)
- `seed`: Random seed (optional)

**Use case:** Testing dropout compensation and error correction algorithms

---

#### `phase-error` Effect (Not implemented yet)

Simulates phase instability in color signal (not yet implemented).

```yaml
- type: "phase-error"
  enabled: true
  phase_jitter_samples: 2.0    # Maximum phase deviation
  frequency_hz: 50.0           # Modulation frequency
  seed: 42
```

**Status:** Reserved for future implementation

---

## Sections Configuration

The `sections` array defines video segments to encode. Each section represents a portion of the output video with specific source material and settings.

### Section Fields

```yaml
sections:
  - name: "Lead-in"
    duration: 1
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
    biphase-vbi:
      disc_area: "lead-in"
```

### `name` (required)
**Type:** String

Section identifier for logging and debugging.

### `duration` (required for some source types)
**Type:** Integer

Duration in **frames** (not seconds).

**Required for:**
- `yuv422-image` sources (static images)
- `png-image` sources (static images)

**Optional/ignored for:**
- `mov-file` sources (duration determined by video file)
- `mp4-file` sources (duration determined by video file)

**Frame counts:**
- PAL: 25 frames/second
- NTSC: ~29.97 frames/second (~30)

Example: 10 seconds PAL = 250 frames

### `source` (required)
**Type:** Object

Defines input media source. See [Source Types](#source-types).

### `sound` (optional)
**Type:** Array

Audio sources for this section. See [Sound Configuration](#sound-configuration).

### `biphase-vbi` (optional)
**Type:** Object

Section-specific LaserDisc metadata. See [Biphase VBI Configuration](#biphase-vbi-configuration).

### `vitc` (optional)
**Type:** Object

Section-specific VITC timecode configuration. See [VITC Configuration](#vitc-configuration).

---

## Source Types

The `source` object defines input media format and location.

### YUV422 Raw Image

Static raw YUV422 image (10-bit or 8-bit per component).

```yaml
source:
  type: "yuv422-image"
  file: "testcard-images/pal-raw/625_50_75_BARS.raw"
```

**Requirements:**
- File must be raw YUV422 format
- Resolution must match video system:
  - PAL: 720×576 (or 702×576)
  - NTSC: 720×480 (or 702×480)
- Section must specify `duration`

---

### PNG Image

Lossless PNG image file.

```yaml
source:
  type: "png-image"
  file: "testcard-images/pal-png/test-pattern.png"
```

**Requirements:**
- Standard PNG format (RGB or RGBA)
- Resolution should match target video system
- Section must specify `duration`

**Advantages:** Easy to create, widely supported

---

### MOV File

QuickTime container with video codec.

```yaml
source:
  type: "mov-file"
  file: "testcard-images/pal-mov/video.mov"
  start_frame: 0    # Optional: starting frame (0-indexed)
```

**Fields:**
- `file`: Path to MOV file
- `start_frame` (optional): Frame offset to begin reading (default: 0)

**Supported codecs:**
- ProRes
- H.264
- Other ffmpeg-compatible codecs

**Duration:** Determined by video file length

---

### MP4 File

MPEG-4 container with video codec.

```yaml
source:
  type: "mp4-file"
  file: "testcard-images/ntsc-mp4/video.mp4"
  start_frame: 0    # Optional: starting frame (0-indexed)
```

**Fields:**
- `file`: Path to MP4 file
- `start_frame` (optional): Frame offset to begin reading (default: 0)

**Supported codecs:**
- H.264
- H.265 (HEVC)
- Other ffmpeg-compatible codecs

**Duration:** Determined by video file length

---

## Sound Configuration

Add audio to video sections. Each section can have one sound field that applies to the entire section duration.

```yaml
sections:
  - name: "Audio Test"
    duration: 100
    source:
      type: "yuv422-image"
      file: "testcard.raw"
    sound:
      type: "sine"
      start_freq_hz: 1000
      end_freq_hz: 2000
```

### Sound Types

The following sound types are supported:

- **silence**: No audio (silence)
- **source**: Use audio from source file (MOV/MP4 only)
- **sine**: Pure sine wave
- **square**: Square wave
- **sawtooth**: Sawtooth wave
- **pink**: Pink noise (1/f noise)
- **white**: White noise (Gaussian)
- **brown**: Brown noise (Brownian/red noise)
- **wav**: Load audio from WAV file

### Silence

```yaml
sound:
  type: "silence"
```

Generates complete silence for the section. This is also the default if no sound field is specified.

---

### Source Audio

```yaml
sound:
  type: "source"
```

Uses the audio track from the source video file. Only works with MOV or MP4 file sources.

**Requirements:**
- Section must use `mov-file` or `mp4-file` as source type
- Source file must contain an audio track

---

### Sine Wave

Generates a pure sine wave tone.

```yaml
sound:
  type: "sine"
  start_freq_hz: 440       # Starting frequency in Hz (required)
  end_freq_hz: 880         # Ending frequency in Hz (optional, defaults to start_freq_hz)
  amplitude: 75            # Volume in percent 0-100 (optional, default 75)
  balance: 0               # Stereo balance -100 to +100 (optional, default 0)
```

**Fields:**
- `start_freq_hz`: Starting frequency in Hz (required)
- `end_freq_hz`: Ending frequency in Hz (optional, defaults to start_freq_hz)
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)
- `seed`: Random seed (optional, not used for sine waves)

**Frequency Sweeps:**
- If `end_freq_hz` is omitted or equals `start_freq_hz`: Constant tone
- If different: Linear frequency sweep across the entire section duration
- Sweep is sample-accurate and calculated across the whole section

**Use cases:**
- Constant reference tones (e.g., 1000 Hz calibration)
- Frequency response testing
- Audio sweep tests
- Stereo balance testing

**Examples:**
```yaml
# Constant 1000 Hz tone at default amplitude (75%)
sound:
  type: "sine"
  start_freq_hz: 1000

# Constant 1000 Hz tone at 50% amplitude
sound:
  type: "sine"
  start_freq_hz: 1000
  amplitude: 50

# Sweep from 20 Hz to 20000 Hz at full amplitude
sound:
  type: "sine"
  start_freq_hz: 20
  end_freq_hz: 20000
  amplitude: 100

# 440 Hz tone on left channel only
sound:
  type: "sine"
  start_freq_hz: 440
  balance: -100

# 440 Hz tone on right channel only
sound:
  type: "sine"
  start_freq_hz: 440
  balance: 100
```

---

### Square Wave

Generates a square wave with 50% duty cycle.

```yaml
sound:
  type: "square"
  start_freq_hz: 440
  amplitude: 60  # Optional, default 75
  balance: 0     # Optional, default 0
```

**Fields:**
- `start_freq_hz`: Starting frequency in Hz (required)
- `end_freq_hz`: Ending frequency in Hz (optional, defaults to start_freq_hz)
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)

**Characteristics:**
- Contains odd harmonics (3rd, 5th, 7th, etc.)
- Useful for testing harmonic distortion
- Harsher sound than sine wave

---

### Sawtooth Wave

Generates a sawtooth wave (linear ramp).

```yaml
sound:
  type: "sawtooth"
  start_freq_hz: 220
  amplitude: 80  # Optional, default 75
  balance: 0     # Optional, default 0
```

**Fields:**
- `start_freq_hz`: Starting frequency in Hz (required)
- `end_freq_hz`: Ending frequency in Hz (optional, defaults to start_freq_hz)
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)

**Characteristics:**
- Contains all harmonics (even and odd)
- Bright, buzzy sound
- Useful for synthesis testing

---

### Pink Noise

Generates pink noise (1/f noise spectrum).

```yaml
sound:
  type: "pink"
  amplitude: 50  # Optional, default 75
  balance: 0     # Optional, default 0
  seed: 42       # Optional random seed for reproducibility
```

**Fields:**
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)
- `seed`: Optional random seed (default: random)

**Characteristics:**
- Equal energy per octave
- More natural sound than white noise
- Useful for acoustic testing and speaker calibration

---

### White Noise

Generates white noise (equal energy at all frequencies).

```yaml
sound:
  type: "white"
  amplitude: 50  # Optional, default 75
  balance: 0     # Optional, default 0
  seed: 42       # Optional random seed
```

**Fields:**
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)
- `seed`: Optional random seed (default: random)

**Characteristics:**
- Flat frequency spectrum
- Hissing sound
- Useful for testing noise floors and dynamic range

---

### Brown Noise

Generates brown noise (Brownian/red noise).

```yaml
sound:
  type: "brown"
  amplitude: 50  # Optional, default 75
  balance: 0     # Optional, default 0
  seed: 42       # Optional random seed
```

**Fields:**
- `amplitude`: Volume as percentage 0-100 (optional, default 75)
- `balance`: Stereo balance -100 (left only) to +100 (right only) (optional, default 0)
- `seed`: Optional random seed (default: random)

**Characteristics:**
- Energy decreases 6 dB per octave
- Deeper, rumbling sound than pink or white noise
- Useful for low-frequency testing

---

### WAV File

Loads audio from a WAV file.

```yaml
sound:
  type: "wav"
  file: "audio/soundtrack.wav"
```

**Fields:**
- `file`: Path to WAV file (required)

**Requirements:**
- Standard WAV format (16-bit PCM, stereo, 44.1kHz recommended)

**Behavior:**
- If WAV is shorter than section: Pads with silence at the end
- If WAV is longer than section: Truncates to section duration
- Mono WAV files are converted to stereo automatically

---

### Audio Technical Details

**Sample Rate:** All audio is generated or resampled to 44.1 kHz stereo (2 channels, interleaved).

**Pre-generation:** Audio for each section is pre-generated in full before encoding begins. This ensures:
- Thread-safe operation with multi-threading
- Accurate frequency sweeps across the entire section
- Consistent audio/video synchronization

**Duration:** The audio duration exactly matches the video section duration (number of fields × samples per field).

**Default:** If no `sound` field is specified, the section will have silence.

---
- Sample rate: 48 kHz recommended
- Channels: Mono or stereo
- Audio loops if video is longer than audio file

---

## Biphase VBI Configuration

Section-level LaserDisc metadata configuration. Used with `biphase-vbi` generator.

```yaml
sections:
  - name: "Programme Content"
    duration: 250
    source:
      type: "yuv422-image"
      file: "testcard.raw"
    biphase-vbi:
      disc_area: "programme-area"
      picture_start: 1
      spec: "standard"
      user_code: "1066"
```

### `disc_area` (required)
**Type:** String  
**Values:** `"lead-in"` | `"programme-area"` | `"lead-out"`

LaserDisc disc region identifier.

- `"lead-in"` - Start of disc (before content)
- `"programme-area"` - Main content area
- `"lead-out"` - End of disc (after content)

### `spec` (optional)
**Type:** String  
**Values:** "standard" | "amendment-2"

Selects the IEC encoding rules used for the six VBI values (3 per field).

- "standard" - IEC 60856/60857 base specification
- "amendment-2" - Applies the amendment 2 CLV picture-number correction (NTSC)

### `user_code` (optional)
**Type:** String (hex)  
**Format:** `X1X3X4X5` (exactly 4 hex digits; optional `0x` prefix)

Provide only the 4 user digits (X1, X3, X4, X5). The encoder will build the
full 24-bit code internally (`8X1DXXX`).

User code is inserted into line 16 (field 1) and line 279/329 (field 2)
for lead-in/lead-out areas when `disc_area` is `lead-in` or `lead-out`.

### CAV Mode Fields

For Constant Angular Velocity (CAV) discs with picture numbering.

```yaml
biphase-vbi:
  disc_area: "programme-area"
  picture_start: 1            # Starting frame number (only needed for first section)
  picture_stop: false         # Optional: enable picture stop code (default: false)
  chapter: 1                  # Optional chapter number
```

**Fields:**
- `picture_start`: Sets the starting picture/frame number for this section
  - Only required for the **first** programme-area section that uses picture numbering
  - Subsequent sections automatically continue from the previous section's last frame number
  - To restart numbering at a different value, specify `picture_start` again
  - Example: Section 1 with `picture_start: 1` and 50 frames → frames 1-50
  - Section 2 (no `picture_start`) with 50 frames → automatically continues as frames 51-100
  - Section 3 with `picture_start: 200` and 30 frames → restarts at frames 200-229
- `picture_stop`: Enable picture stop code (optional, default: `false`)
  - When `true`, inserts picture stop code (0x82CFFF) on lines 16/17 (or 279/280 for NTSC) of field 2
  - Picture stop code enables LaserDisc players to automatically pause on the selected frame
  - When `false` (default), uses programme status code instead
  - Only applies to CAV mode with picture numbering
- `chapter`: Optional chapter/track number for this section
  - Chapter changes do NOT reset picture numbers
  - Picture numbers continue incrementing even when changing chapters

**Important Notes:**
- Picture numbers automatically increment across sections and chapter boundaries
- Lead-in and lead-out areas never have picture numbers
- Picture numbers only reset when explicitly set with a new `picture_start` value

### CLV Mode Fields

For Constant Linear Velocity (CLV) discs with timecode.

```yaml
biphase-vbi:
  disc_area: "programme-area"
  chapter: 1                        # Optional chapter number
  timecode_start: "00:00:00.00"     # Starting timecode (HH:MM:SS.FF)
```

**Fields:**
- `chapter`: Optional chapter/track number for this section
  - Can be used in both CAV and CLV modes
  - Chapter changes do NOT reset timecode or picture numbers
- `timecode_start`: Starting timecode in format `HH:MM:SS.FF`
  - HH: Hours (00-99)
  - MM: Minutes (00-59)
  - SS: Seconds (00-59)
  - FF: Frames (00-24 PAL, 00-29 NTSC)
  - Only required for the **first** programme-area section that uses timecode
  - Subsequent sections automatically continue from the previous section's ending timecode
  - To restart timecode at a different value, specify `timecode_start` again

**Important Notes:**
- Timecode increments automatically based on frame rate across sections and chapter boundaries
- Lead-in and lead-out areas never have timecode or picture numbers
- Timecode only resets when explicitly set with a new `timecode_start` value

---

## VITC Configuration

Section-level VITC (Vertical Interval Time Code) configuration for consumer tape formats. Used with `vitc` generator.

```yaml
sections:
  - name: "Lead-in"
    duration: 2
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
    vitc:
      timecode_start: "00:00:00.00"
  
  - name: "Content"
    duration: 50
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_100_BARS.raw"
    vitc:
      timecode_start: "00:00:00.00"
```

### `timecode_start` (optional)
**Type:** String  
**Format:** `HH:MM:SS.FF`

Starting timecode for this section in format `HH:MM:SS.FF`:
- HH: Hours (00-99)
- MM: Minutes (00-59)
- SS: Seconds (00-59)
- FF: Frames (00-24 for PAL, 00-29 for NTSC)

**Behavior:**
- Only required for the **first** section that uses VITC timecode
- Subsequent sections automatically continue from the previous section's ending timecode
- To restart timecode at a different value, specify `timecode_start` again in a later section

**Example:**
```yaml
sections:
  # Section 1: Starts at 00:00:00.00, duration 50 frames → ends at 00:00:02.00 (PAL)
  - name: "Intro"
    duration: 50
    source:
      type: "yuv422-image"
      file: "intro.raw"
    vitc:
      timecode_start: "00:00:00.00"
  
  # Section 2: Automatically continues from 00:00:02.00
  - name: "Main"
    duration: 100
    source:
      type: "mov-file"
      file: "main.mov"
    # No vitc block needed - continues from previous section
  
  # Section 3: Restart at a specific timecode
  - name: "Credits"
    duration: 50
    source:
      type: "yuv422-image"
      file: "credits.raw"
    vitc:
      timecode_start: "01:00:00.00"  # Jump to 1 hour
```

**Important Notes:**
- Timecode increments automatically based on frame rate (25 fps for PAL, ~30 fps for NTSC)
- If no section specifies `timecode_start`, VITC will start from frame 0 (00:00:00.00)
- The pipeline-level `vitc` generator must be enabled with appropriate VBI lines

---

## Complete Examples

### Basic PAL Composite

```yaml
name: "Simple PAL Test"
description: "Basic PAL composite encoding"

output:
  filename: "output/pal-basic"
  format: "pal-composite"

pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true
  metadata:
    generators:
      - type: "color-burst"
        enabled: true

sections:
  - name: "Content"
    duration: 250  # 10 seconds at 25fps
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
```

### NTSC with Effects

```yaml
name: "NTSC with Noise and Dropouts"
description: "Simulates degraded VHS tape"

output:
  filename: "output/ntsc-degraded"
  format: "ntsc-composite"

pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true
  
  metadata:
    generators:
      - type: "color-burst"
        enabled: true
      - type: "vitc"
        enabled: true
        lines: [14, 276]
        start_frame_offset: 0
  
  effects:
    - type: "noise"
      enabled: true
      snr_db: 30.0
      seed: 42
    
    - type: "dropout"
      enabled: true
      density: 0.005
      multi_field_probability: 0.20
      single_field_probability: 0.80
      seed: 123

sections:
  - name: "Test Pattern"
    duration: 300  # ~10 seconds at ~30fps
    source:
      type: "png-image"
      file: "testcard-images/ntsc-png/smpte-bars.png"
```

### PAL LaserDisc CAV with VITS

```yaml
name: "PAL LaserDisc CAV"
description: "LaserDisc with test signals and frame numbering"

output:
  filename: "output/laserdisc-cav"
  format: "pal-composite"

laserdisc:
  mode: "cav"

pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true
  
  metadata:
    generators:
      - type: "biphase-vbi"
        enabled: true
        lines: [16, 17, 18, 328, 329, 330]
        format: "picture-number"
      
      - type: "vits-pal"
        enabled: true
        signals:
          - line: 13
            signal: "multiburst"
          - line: 19
            signal: "uk-national"
          - line: 325
            signal: "itu-combination"
      
      - type: "color-burst"
        enabled: true

sections:
  - name: "Lead-in"
    duration: 1
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
    biphase-vbi:
      disc_area: "lead-in"
  
  # Chapter 1 starts at frame 1
  - name: "Chapter 1 Intro"
    duration: 250  # Frames 1-250
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_100_BARS.raw"
    biphase-vbi:
      disc_area: "programme-area"
      picture_start: 1  # Start at frame 1
      chapter: 1
  
  - name: "Chapter 1 Content"
    duration: 250  # Frames 251-500 (automatically continues)
    source:
      type: "mov-file"
      file: "video/content.mov"
      start_frame: 0
    biphase-vbi:
      disc_area: "programme-area"
      chapter: 1  # No picture_start needed - continues from 251
  
  # Chapter 2 continues numbering (doesn't reset)
  - name: "Chapter 2"
    duration: 300  # Frames 501-800 (continues from previous)
    source:
      type: "mov-file"
      file: "video/chapter2.mov"
      start_frame: 0
    biphase-vbi:
      disc_area: "programme-area"
      chapter: 2  # Chapter change doesn't reset picture numbers
  
  - name: "Lead-out"
    duration: 1
    source:
      type: "yuv422-image"
      file: "testcard-images/pal-raw/625_50_75_BARS.raw"
    biphase-vbi:
      disc_area: "lead-out"
```

### Y/C Output with Audio

```yaml
name: "PAL Y/C with Audio"
description: "Separate luma/chroma output with soundtrack"

output:
  filename: "output/yc-audio"
  format: "pal-yc"
  sound_format: "pcm"

pipeline:
  preprocessing:
    filters:
      chroma:
        enabled: true
  
  metadata:
    generators:
      - type: "vitc"
        enabled: true
        lines: [19, 20, 331, 332]
        start_frame_offset: 0
      - type: "color-burst"
        enabled: true

sections:
  - name: "Intro"
    duration: 125  # 5 seconds
    source:
      type: "png-image"
      file: "images/title.png"
    sound:
      - type: "sine"
        start_freq_hz: 1000
        end_freq_hz: 1000
        hz_per_field: 0
  
  - name: "Main Content"
    duration: 500  # 20 seconds
    source:
      type: "mp4-file"
      file: "video/main.mp4"
      start_frame: 0
    sound:
      - type: "wav"
        file: "audio/soundtrack.wav"
```