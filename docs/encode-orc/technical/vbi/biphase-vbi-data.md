# PAL and NTSC biphase code

Note: This is used for both PAL and NTSC appearing on field lines 16,17,18

See ld-decode tools/ld-process-vbi for decoding details (and the meaning of the data)

## 24-bit Biphase Code Timing and Encoding Description

### Overview
The diagram describes a **24-bit biphase-encoded serial signal** with defined timing, voltage levels, and bit interpretation rules. Each bit is transmitted using **biphase (Manchester-style) encoding**, where the **transition in the center of the bit cell** determines the logical value.

---

### Bit Encoding Rules
- **Logical `1`**  
  Represented by a **positive (low-to-high) transition** at the **center of the bit cell**.
- **Logical `0`**  
  Represented by a **negative (high-to-low) transition** at the **center of the bit cell**.

---

### Bit Order
- Transmission is **MSB first** (Most Significant Bit).
- Ends with **LSB** (Least Significant Bit).

Example shown in the diagram:
- Bit 1 (MSB): logic `1`
- Bit 2: logic `0`
- Bit 3: logic `0`
- Bit 4 (LSB): logic `1`

---

### Timing Characteristics
- **Bit length:**  
  `2.0 µs ± 0.01 µs` per bit  
  (i.e. **2 µs/bit**)

- **Total frame length:**  
  24 bits × 2 µs = **48 µs**

- **Transition timing (Detail “A”):**  
  - Normal case:  
    `T = 0.188 H ± 0.003 H`
  - **Status code only** (sub-clause 10.1.8):  
    `T = 0.172 H ± 0.003 H`

(`H` represents the horizontal line period used as the timing reference.)

---

### Signal Integrity
- **Rise and fall times:**  
  `225 ns ± 25 ns` (measured from 10% to 90%)

---

### Voltage Levels
- High level: **+5 V** (±5%)
- Low level: **0 V**
- Output impedance and levels are shown schematically to indicate compliant logic-level signaling.

---

### Summary
This signal format is a **self-clocking 24-bit biphase serial code** with:
- Fixed 2 µs bit cells  
- Data encoded by the **direction of the mid-bit transition**
- MSB-first transmission
- Strict rise/fall and timing tolerances
- A special reduced-timing variant for **status codes only**

The enlarged **Detail “A”** illustrates the exact transition placement used to decode individual bits.
```
