# Comprehensive Unit Tests for encode-orc

This test-projects folder contains comprehensive unit tests for the encode-orc project. Each test file covers one of the six basic configurations while exercising as much functionality as possible.

## Test Files

All test files are located in the `test-projects/` directory.

### 1. PAL CAV Composite (`test-projects/test-comprehensive-pal-cav-composite.yaml`)
Tests PAL composite output with CAV (Constant Angular Velocity) LaserDisc mode.

**Features tested:**

- PAL composite format output
- CAV picture numbering with auto-increment
- Biphase VBI encoding on lines 16-18, 328-330
- VITS-PAL signals (multiburst, uk-national, itu-combination, itu-composite)
- Color burst generation
- Chroma filtering (enabled), luma filtering (disabled)
- Noise effect (SNR 42dB, seed 12345)
- Dropout effect (density 0.0005, seeds 67890)
- Lead-in/lead-out areas with user code "ABCD"
- Multiple chapters (1, 2, 3) without picture number reset
- Picture stop code testing (disabled)
- Standard spec encoding
- Multiple audio types: silence, sine, square, pink noise, frequency sweep
- Audio balance and amplitude variations
- Total frames: 10 (1 lead-in + 2+2+2+2 programme + 1 lead-out)

### 2. PAL CLV Composite (`test-projects/test-comprehensive-pal-clv-composite.yaml`)
Tests PAL composite output with CLV (Constant Linear Velocity) LaserDisc mode.

**Features tested:**

- PAL composite format output
- CLV timecode with auto-increment (starts at 00:00:00.00)
- Biphase VBI timecode encoding on lines 16-18, 328-330
- VITS-PAL signals (multiburst, uk-national, itu-combination)
- Color burst generation
- Chroma filtering (enabled), luma filtering (disabled)
- Noise effect (SNR 45dB, seed 22222)
- Dropout effect (density 0.001, seeds 33333)
- Lead-in/lead-out areas with user code "5678"
- Multiple chapters (1, 2, 3) without timecode reset
- Standard spec encoding
- Multiple audio types: silence, sawtooth, white noise, frequency sweep, brown noise
- Audio balance and amplitude variations
- Total frames: 10 (1 lead-in + 2+2+2+2 programme + 1 lead-out)

### 3. PAL VITC YC (`test-projects/test-comprehensive-pal-vitc-yc.yaml`)
Tests PAL Y/C (S-Video) output with VITC timecode.

**Features tested:**

- PAL Y/C (separate luma/chroma) format output
- VITC encoding on lines 19, 20, 331, 332
- VITC timecode with auto-increment (starts at 00:00:00.00)
- VITS-PAL signals (multiburst, itu-composite)
- Color burst generation
- Chroma filtering (enabled), luma filtering (enabled)
- Noise effect only (SNR 38dB, seed 55555)
- Multiple audio types: sine, square, sawtooth frequency sweep, pink noise, white noise
- Audio balance and amplitude variations
- Total frames: 10 (5 sections × 2 frames each)

### 4. NTSC CAV Composite (`test-projects/test-comprehensive-ntsc-cav-composite.yaml`)
Tests NTSC composite output with CAV LaserDisc mode.

**Features tested:**

- NTSC composite format output
- CAV picture numbering with auto-increment
- Biphase VBI encoding on lines 16-18, 278-280
- VITS-NTSC signals (ntc7-composite, vir, ntc7-combination, multiburst)
- Color burst generation
- Chroma filtering (enabled), luma filtering (disabled)
- Noise effect (SNR 40dB, seed 11122)
- Dropout effect (density 0.0008, seeds 33344)
- Lead-in/lead-out areas with user code "1234"
- Multiple chapters (1, 2, 3) without picture number reset
- Picture stop code testing (enabled in chapter 2)
- Amendment-2 spec encoding
- Multiple audio types: silence, sine, square, frequency sweep, brown noise
- Audio balance and amplitude variations
- Total frames: 10 (1 lead-in + 2+2+2+2 programme + 1 lead-out)

### 5. NTSC CLV Composite (`test-projects/test-comprehensive-ntsc-clv-composite.yaml`)
Tests NTSC composite output with CLV LaserDisc mode.

**Features tested:**

- NTSC composite format output
- CLV timecode with auto-increment (starts at 00:00:00.00)
- Biphase VBI timecode encoding on lines 16-18, 278-280
- VITS-NTSC signals (ntc7-composite, vir, ntc7-combination)
- Color burst generation
- Chroma filtering (enabled), luma filtering (disabled)
- Noise effect (SNR 43dB, seed 66677)
- Dropout effect (density 0.0012, seeds 77788)
- Lead-in/lead-out areas with user code "9ABC"
- Multiple chapters (1, 2, 3) without timecode reset
- Amendment-2 spec encoding
- Multiple audio types: silence, sawtooth, white noise, frequency sweep, pink noise
- Audio balance and amplitude variations
- Total frames: 10 (1 lead-in + 2+2+2+2 programme + 1 lead-out)

### 6. NTSC VITC YC (`test-projects/test-comprehensive-ntsc-vitc-yc.yaml`)
Tests NTSC Y/C output with VITC timecode.

**Features tested:**

- NTSC Y/C (separate luma/chroma) format output
- VITC encoding on lines 14, 19, 276, 281
- VITC timecode with auto-increment (starts at 00:00:00.00)
- VITS-NTSC signals (ntc7-composite, vir)
- Color burst generation
- Chroma filtering (enabled), luma filtering (enabled)
- Noise effect only (SNR 36dB, seed 34343)
- Multiple audio types: sine, square, sawtooth frequency sweep, brown noise, white noise
- Audio balance and amplitude variations
- Total frames: 10 (5 sections × 2 frames each)

## Running the Tests

To run all tests:

```bash
cd /home/sdi/Coding/encode-orc/build
./encode-orc ../test-projects/test-comprehensive-pal-cav-composite.yaml
./encode-orc ../test-projects/test-comprehensive-pal-clv-composite.yaml
./encode-orc ../test-projects/test-comprehensive-pal-vitc-yc.yaml
./encode-orc ../test-projects/test-comprehensive-ntsc-cav-composite.yaml
./encode-orc ../test-projects/test-comprehensive-ntsc-clv-composite.yaml
./encode-orc ../test-projects/test-comprehensive-ntsc-vitc-yc.yaml
```

## Feature Coverage Summary

### Output Formats
- PAL composite
- NTSC composite
- PAL Y/C
- NTSC Y/C
- TBC writer
- PCM sound format

### LaserDisc Modes
- CAV with picture numbering
- CLV with timecode
- Lead-in/lead-out areas
- User codes
- Chapter markers
- Picture stop codes
- Standard spec
- Amendment-2 spec

### Metadata Generators
- Color burst
- Biphase VBI (picture-number format)
- Biphase VBI (timecode format)
- VITC timecode
- VITS-PAL (multiburst, uk-national, itu-combination, itu-composite)
- VITS-NTSC (ntc7-composite, ntc7-combination, vir, multiburst)

### Preprocessing
- Chroma filter enabled
- Chroma filter disabled
- Luma filter enabled
- Luma filter disabled

### Effects
- Noise effect with various SNR levels
- Dropout effect with various densities
- Random seeds for reproducibility
- Multi-field and single-field dropout probabilities

### Audio Generation
- Silence
- Sine wave (constant frequency)
- Sine wave (frequency sweep)
- Square wave
- Sawtooth wave
- Pink noise
- White noise
- Brown noise
- Audio amplitude control (0-100%)
- Audio balance control (-100 to +100)
- Random seeds for noise

### Source Types
- YUV422 raw images (all tests use this to minimize dependencies)

### Timecode/Numbering
- Auto-increment picture numbers across sections
- Auto-increment timecode across sections
- Chapter changes without reset
- Custom timecode start values
- Custom picture number start values

## Test Design Philosophy

Each test is designed to:
1. **Minimize execution time**: Only 10 frames per test (minimum 4 met)
2. **Maximize feature coverage**: Exercise as many different features as possible
3. **Test interactions**: Verify that features work together correctly
4. **Ensure reproducibility**: Use fixed random seeds for consistent results
5. **Cover edge cases**: Test auto-increment, chapter changes, different specs, etc.

## Expected Outputs

Each test will generate:
- TBC file(s) in `test-output/` directory
- JSON metadata file
- PCM audio file
- Console output showing encoding progress

Verify successful execution by checking:
1. Exit code is 0
2. Output files exist and have non-zero size
3. No error messages in console output
4. JSON metadata contains expected structure
