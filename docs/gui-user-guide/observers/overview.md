# Overview

Observers are components that analyze video fields and extract metadata, quality metrics, and encoded information from the video signal. They populate the observation context with their findings, which can then be used by other pipeline stages or displayed to the user.

Each observer specializes in extracting specific types of information from the video signal, ranging from VBI (Vertical Blanking Interval) data to quality measurements.

## Biphase Observer

**Purpose:** Extracts biphase-encoded (Manchester-encoded) VBI data from LaserDisc video signals.

**What it extracts:**
- CAV (Constant Angular Velocity) picture numbers
- Chapter numbers
- CLV (Constant Linear Velocity) timecodes (hours, minutes, seconds, picture)
- Lead-in and lead-out codes
- Picture stop codes
- User codes
- Programme status information (CX audio, disc size, side number, teletext, digital video, sound mode, FM multiplex, programme dump, parity)
- Amendment 2 status (copy permissions, video standard, sound mode)

**How it works:**
The observer reads VBI data from lines 16, 17, and 18 of the video field. It decodes the Manchester-encoded biphase data and interprets it according to the IEC 60856/60857 LaserDisc standards. The decoded data is validated using parity checks and BCD (Binary Coded Decimal) decoding.

**What it looks like:**
The extracted VBI data appears as metadata fields in the observation context, providing frame-accurate information about disc position, chapter markers, and disc configuration.

---

## Black PSNR Observer

**Purpose:** Measures the Peak Signal-to-Noise Ratio from VITS (Vertical Interval Test Signals) black level reference signals.

**What it measures:**
- Black level PSNR in decibels (dB)

**How it works:**
The observer analyzes specific regions of the VBI that contain black level test signals. It extracts sample data from these regions and calculates the PSNR by measuring the noise variance relative to the expected black level. The measurement helps assess the quality of the black level region of the video signal.

**What it looks like:**
A single numerical value in dB representing the signal quality in the black regions. Higher values indicate cleaner signals with less noise.

---

## White SNR Observer

**Purpose:** Measures Signal-to-Noise Ratio from VITS white flag test signals.

**What it measures:**
- White flag SNR in decibels (dB)

**How it works:**
The observer analyzes white flag test signals in the VBI to measure noise in the white level region of the video signal. It extracts samples from specific regions, uses the mean of the data as the signal reference, and calculates the SNR by comparing signal strength to noise variance. This measurement complements the Black PSNR measurement to provide a complete picture of signal quality across the luminance range.

**What it looks like:**
A numerical value in dB representing the signal-to-noise ratio in white regions. Higher values indicate cleaner signals with less noise at white levels.

---

## Burst Level Observer

**Purpose:** Analyzes the color burst signal amplitude.

**What it measures:**
- Median color burst amplitude in IRE units

**How it works:**
The observer examines the color burst signal (the reference signal for color decoding located after the horizontal sync pulse) across multiple lines. It measures the amplitude of the burst and calculates the median value in IRE (Institute of Radio Engineers) units. The burst level is important for color reproduction quality and can indicate signal degradation or processing artifacts.

**What it looks like:**
A numerical value representing the burst amplitude in IRE units. Standard color burst amplitude should be approximately 40 IRE for NTSC signals.

---

## Closed Caption Observer

**Purpose:** Extracts EIA-608 closed caption data from the VBI.

**What it extracts:**
- Closed caption presence indicator
- First EIA-608 data byte (7-bit + parity)
- Second EIA-608 data byte (7-bit + parity)
- Parity validation for both bytes

**How it works:**
The observer reads line 21 for NTSC (or line 22 for PAL) from the second field only (where closed captions are encoded). It decodes the NRZ (Non-Return-to-Zero) encoded caption data by detecting the start bits (00 followed by 1) and then reading 7 data bits plus 1 parity bit for each of the two caption bytes. The parity is validated to ensure data integrity.

**What it looks like:**
Two bytes of caption data per field, which can be decoded into text characters and control codes by caption decoders. The data appears only on field 2 of NTSC video.

---

## Field Quality Observer

**Purpose:** Calculates overall quality metrics for each video field.

**What it measures:**
- Field quality score (0.0-1.0)
- Dropout count
- Phase correctness indicator

**How it works:**
The observer combines multiple quality indicators to produce an overall quality score:
- **Dropout density**: Counts the number of dropouts detected in the field
- **Phase correctness**: Validates that the field's phase information is correct
- **Signal metrics**: Incorporates SNR and other signal quality measurements when available

The quality score is used by the disc mapping policy to select the best duplicate when multiple fields have the same VBI frame number (common when stacking multiple captures).

**What it looks like:**
A normalized quality score from 0.0 (worst) to 1.0 (best), along with individual metrics. Higher scores indicate better-quality fields with fewer dropouts and better signal characteristics.

---

## FM Code Observer

**Purpose:** Extracts FM code from NTSC LaserDisc signals.

**What it extracts:**
- FM code presence indicator
- 20-bit FM code data payload
- Field indicator flag

**How it works:**
The observer reads line 10 of NTSC video fields, where FM codes are encoded. It decodes the frequency-modulated signal to extract a 20-bit data value and a field flag bit. FM codes can carry various types of metadata specific to certain LaserDisc formats.

**What it looks like:**
A 20-bit data value and a boolean field flag when present. Not all LaserDiscs contain FM codes, so the presence indicator shows whether valid FM code data was decoded.

---

## White Flag Observer

**Purpose:** Detects the presence of white flag signals on NTSC LaserDisc video.

**What it detects:**
- White flag presence indicator

**How it works:**
The observer examines line 11 of NTSC video fields for the white flag signal. The white flag is a reference signal that can be used for various test and measurement purposes. It indicates the presence of this test signal for quality analysis.

**What it looks like:**
A simple boolean indicator showing whether a white flag was detected on the current field.
