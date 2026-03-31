# LaserDisc PAL VITS

**Reference:**
*Video Demystified, 5th Edition* — ISBN 978-0-750-68395-1

Assumptions unless stated otherwise:

* 75 Ω terminated composite video
* 1 V p-p nominal (PAL), 1 V p-p NTSC (with −40 to −43 IRE sync)
* Timing measured from leading edge of horizontal sync

---

## PAL System Reference (625/50)

| Parameter                | Value                 |
| ------------------------ | --------------------- |
| Line period              | 64.0 µs               |
| Active video             | ≈52 µs                |
| Horizontal sync          | ≈4.7 µs               |
| Front porch              | ≈1.65 µs              |
| Back porch (incl. burst) | ≈5.7 µs               |
| Colour subcarrier        | 4.43361875 MHz        |
| Burst length             | ~10 cycles (≈2.25 µs) |
| Sync level               | −43 IRE (≈−300 mV)    |
| Blank level              | 0 IRE                 |
| Peak white               | +100 IRE (≈700 mV)    |

---

## ITU Composite Test Signal for PAL (Figure 8.41)
![Video Demystified P331 Figure 8.41](assets/ITU-Composite-PAL-with-IRE.png)
**VITS line:** 19

### Description
The ITU (BT.628 and BT.473) has developed a composite test signal that may be used to test several video parameters, rather than using multiple test signals. The ITU composite test signal for PAL systems (shown in Figure 8.41) consists of a white flag, a 2T pulse, and a 5-step modulated staircase signal.

The white flag has a peak amplitude of 100 ±1 IRE and a width of 10 µs.

The 2T pulse has a peak amplitude of 100 ±0.5 IRE, with a half-amplitude width of 200 ±10 ns.

The 5-step modulated staircase signal consists of 5 luminance steps (whose IRE values are shown in Figure 8.41) superimposed with a 42.86 ±0.5 IRE subcarrier that has a phase of 60° ±1° relative to the U axis. The rise and fall times of each modulation packet envelope are approximately 1 µs.

### Timing Breakdown (approx.)

* Sync → burst end: ~12 µs
* 100 IRE flat white: ~12 µs to ~22 µs
* 2T pulse: 
  * centered on 26 µs
  * (bandwidth stress at ~2.2 MHz)
* Staircase section begins: ~30 µs (30 µs, 40 µs, 44 µs, 48 µs, 52 µs, 56 µs, 60 µs)
* End of active content: ~60 µs

### Levels

* Sync: −43 IRE
* Burst centered on blank (±20 IRE chroma amplitude)
* Staircase steps: 0, 20, 40, 60, 80, 100 IRE

---

## United Kingdom PAL National Test Signal #1 (Figure 8.42)
![Video Demystified P332 Figure 8.42](assets/UK-PAL-National.png)
**VITS line:** 332

### Description
U.K. Version

The United Kingdom allows the use of a slightly different test signal since the 10T pulse is more sensitive to delay errors than the 20T pulse (at the expense of occupying less chrominance bandwidth). Selection of an appropriate pulse width is a trade-off between occupying the PAL chrominance bandwidth as fully as possible and obtaining a pulse with sufficient sensitivity to delay errors. Thus, the national test signal (developed by the British Broadcasting Corporation and the Independent Television Authority) in Figure 8.42 may be present on lines 19 and 332 for (I) PAL systems in the United Kingdom.

The white flag has a peak amplitude of 100 ±1 IRE and a width of 10 µs.

The 2T pulse has a peak amplitude of 100 ±0.5 IRE, with a half-amplitude width of 200 ±10 ns.

The 10T chrominance pulse has a peak amplitude of 100 ±0.5 IRE.

The 5-step modulated staircase signal consists of 5 luminance steps (whose IRE values are shown in Figure 8.42) superimposed with a 21.43 ±0.5 IRE subcarrier that has a phase of 60° ±1° relative to the U axis. The rise and fall times of each modulation packet envelope is approximately 1 µs.

### Distinguishing Features

* Replaces the 12.5T pulse (ITU) with a **10T pulse**
* Slightly different staircase geometry

### Timing Notes

* Sync + burst complete by ~12 µs
* 100 IRE reference: ~12–22 µs
* 2T pulse: centered on 26 µs
* 10T pulse: centered 30 µs
* Staircase: ~34–60 µs

---

## ITU Combination ITS Test Signal for PAL (Figure 8.45)
![Video Demystified P335 Figure 8.45](assets/ITU-Combination-ITS-PAL.png)
**VITS line:** 20

### Description
ITU ITS Version for PAL

The ITU (BT.473) has developed a combination ITS (insertion test signal) that may be used to test several PAL video parameters, rather than using multiple test signals. The ITU combination ITS for PAL systems (shown in Figure 8.45) consists of a 3-step modulated pedestal with peak-to-peak amplitudes of 20, 60, and 100 ±1 IRE, and an extended subcarrier packet with a peak-to-peak amplitude of 60 ±1 IRE. The rise and fall times of each subcarrier packet envelope are approximately 1 µs.

The phase of each subcarrier packet is 60° ±1° relative to the U axis. The tolerance on the 50 IRE level is ±1 IRE.

The ITU composite ITS may be present on line 331.

### Structure

1. Sync + burst
2. Multi-level luminance steps (40-60, 20-80, 0-100 IRE)
3. 20-80 IRE reference block

### Timing Highlights

* Luminance steps occupy ~14–28 µs
* IRE reference block ~34–60 µs

---

## ITU Multiburst Test Signal for PAL (Figure 8.38)
![Video Demystified P329 Figure 8.38](assets/multiburst-PAL.png)
**VITS line:** 333

### Description
The ITU multiburst test signal for (B, D, G, H, I) PAL, shown in Figure 8.38, consists of a 4 µs white flag with a peak amplitude of 80 ±1 IRE and six frequency packets, each a specific frequency. The packets have a 50 ±1 IRE pedestal with peak-to-peak amplitudes of 60 ±0.5 IRE. Note the starting and ending points of each packet are at zero phase. The gaps between packets are 0.4–2.0 µs. The ITU multiburst test signal may be present on line 18.

The multiburst signals are used to test the frequency response of the system by measuring the peak-to-peak amplitudes of the packets.

### Burst Frequencies (luminance multiburst)

| Segment | Frequency |
| ------- | --------- |
| 1       | 0.5 MHz   |
| 2       | 1.0 MHz   |
| 3       | 2.0 MHz   |
| 4       | 4.0 MHz   |
| 5       | 4.8 MHz   |
| 6       | 5.8 MHz   |

### Timing

* 80 IRE reference bank
* Multiburst section: 24 µs, 30 µs, 36 µs, 42 µs, 48 µs, 54
* Level: 20-80 IRE