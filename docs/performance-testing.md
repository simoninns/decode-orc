# EFM Decode Performance Testing

Tracking decode performance of `orc-cli` against the standalone `ld-decode-tools`
(`efm-decoder-f2`, `efm-decoder-f1`, `efm-decoder-d24`) baseline.

---

## System

- **Machine:** titan (sdi)
- **OS:** Linux
- **Build type:** Debug (`CMAKE_BUILD_TYPE=Debug`) — confirmed for both orc and the
  standalone tools via `CMakeCache.txt`

---

## Baseline — standalone `ld-decode-tools` (efm-decoder-*)

These timings were recorded prior to the orc integration and are documented in full
in `docs/efm-golden-sample-baseline.md`.  Each stage is a separate process communicating
via intermediate files on disk.

### Audio decode — `roger_rabbit`

| Stage | Tool | Internal processing time | `real` wall time |
|---|---|---|---|
| EFM → F2  | `efm-decoder-f2` | 14,184 ms (14.18 s) | 0m29.478s |
| F2 → D24  | `efm-decoder-f1` | 32,512 ms (32.51 s) | 0m41.445s |
| D24 → WAV | `efm-decoder-d24` | 3,839 ms (3.84 s)  | 0m15.780s |
| **Total** | | **50,535 ms (50.5 s)** | **~1m26.7s** |

Stage breakdown for EFM → F2:
- Channel to F3: 12,190 ms
- F3 to F2 section: 1,947 ms
- F2 correction: 46 ms

Stage breakdown for F2 → D24:
- F2 to F1: 29,448 ms
- F1 to Data24: 3,063 ms

Stage breakdown for D24 → WAV:
- Data24 to Audio: 3,127 ms
- Audio correction: 711 ms

### Data decode — `DS2_comS1`

| Stage | Tool | Internal processing time | `real` wall time |
|---|---|---|---|
| EFM → F2  | `efm-decoder-f2` | 19,301 ms (19.30 s) | 0m37.797s |
| F2 → D24  | `efm-decoder-f1` | 40,140 ms (40.14 s) | 0m51.215s |
| D24 → BIN | `efm-decoder-d24` | 15,359 ms (15.36 s) | 0m15.955s |
| **Total** | | **74,800 ms (74.8 s)** | **~1m44.97s** |

Stage breakdown for EFM → F2:
- Channel to F3: 15,096 ms
- F3 to F2 section: 2,415 ms
- F2 correction: 1,789 ms

Stage breakdown for F2 → D24:
- F2 to F1: 36,415 ms
- F1 to Data24: 3,724 ms

Stage breakdown for D24 → BIN:
- Data24 to Raw Sector: 14,466 ms
- Raw Sector to Sector: 892 ms

---

## Current — `orc-cli` integrated pipeline

- **Git commit:** `8fad4bf` (tag `v1.0.3-3-g8fad4bf`, branch `issue56-202603`)
- **Commit message:** "Updated all package building to include ezpwd library references
  required for EFM decoder"
- **Command template:**
  ```bash
  time ./build/bin/orc-cli test-projects/<project>.orcprj --process --log-level warn
  ```

Note: orc-cli runs the entire pipeline in-process (no intermediate files written between
stages), so disk I/O between stages is eliminated versus the baseline.

### Audio decode — `roger_rabbit`

| | `real` | `user` | `sys` |
|---|---|---|---|
| orc-cli | **7m2.271s (422.3s)** | 6m59.511s | 0m1.110s |
| Baseline (sum of real) | ~1m26.7s (86.7s) | — | — |
| **Slowdown (wall time)** | **~4.9×** | | |

### Data decode — `DS2_comS1`

| | `real` | `user` | `sys` |
|---|---|---|---|
| orc-cli | **8m20.223s (500.2s)** | 8m16.320s | 0m1.513s |
| Baseline (sum of real) | ~1m44.97s (105.0s) | — | — |
| **Slowdown (wall time)** | **~4.8×** | | |

---

## Profiling — `perf` via nix devShell (2026-03-10)

### Setup

Added `linuxPackages.perf` and `hotspot` to the nix devShell (`flake.nix`) and entered
the shell with `nix develop`.  Then ran against `roger_rabbit` (audio decode, ~430 s).

### `perf stat` hardware counters

```
2,006,755,010,908  cycles:u
5,242,901,352,559  instructions:u          #  2.61 insn/cycle
1,726,804,883      cache-misses:u          #  5.77% of all cache refs
29,929,154,156     cache-references:u
1,542,534,815      branch-misses:u
15,897,063         dTLB-load-misses
429.97 s elapsed  /  424.1 s user  /  1.1 s sys
```

**5.24 trillion instructions** in 430 s is far more work than the ~50.5 s of internal
algorithmic work the standalone tools do.  The IPC of 2.61 shows the CPU is efficiently
executing those instructions — the problem is the sheer volume of them.

### `perf record -g` flat profile — top self-time functions

Captured with `perf record -F 99 -g` (42,196 samples).

| Self % | Symbol |
|---|---|
| 3.32 | `std::vector<uint8_t>::size()` (via `_M_range_check` → `at()` in `ChannelToF3Frame`) |
| 2.69 | `__normal_iterator<uint8_t*>::__normal_iterator()` (iterator construction in `push_back`) |
| 2.47 | `ezpwd::reed_solomon::decode_symbols` (C1 + C2 CIRC, split ~1.32 / 1.16) |
| 2.38 | `std::vector<uint8_t>::operator[]` |
| 2.07 | `std::vector<uint8_t>::emplace_back` |
| 2.03 | `std::vector<uint8_t>::vector(const&)` — **copy constructor** |
| 1.97 | `ChannelToF3Frame::tvaluesToData()` — actual EFM symbol decode |
| 1.96 | `__normal_iterator<DelayContents_t*>::__normal_iterator()` (delay line iterator) |
| 1.67 | `__normal_iterator<uint8_t const*>::__normal_iterator()` |
| 1.65 | `DelayLines::push(vector&, vector&, vector&)` |
| 1.62 | `_int_malloc` |
| 1.30 | `__copy_m<uint8_t>` — memory copy |
| 1.22 | `__normal_iterator<DelayContents_t*>::operator+(long)` |
| 1.17 | `ezpwd::reed_solomon_tabs::modnn()` — GF arithmetic |
| 1.10 | `std::vector<uint8_t>::~vector()` — destructor |
| 1.08 | `_int_free` |
| 1.05 | `malloc_consolidate` |
| 1.04 | `std::forward<uint8_t>` |
| 1.01 | `_Vector_base::_M_create_storage` |

---

## Root cause analysis

### 1. STL containers compiled at `-O0` — the biggest factor (~20% of CPU)

`orc` compiles with `-O0 -g` (confirmed via `compile_commands.json`). At `-O0`, **all
C++ STL template code is inlined but not optimised**: every `push_back`, iterator
construction, `size()`, `operator[]`, and copy constructor becomes a sequence of
unoptimised function calls with no elision, no SIMD, and no dead-code removal.

The profile shows ~20 % of CPU time spent in pure vector bookkeeping that would
vanish entirely in a `-O2` build (inlined to a handful of instructions or zero).

### 2. The baseline comparison is misleading — Qt vs STL at `-O0`

The old `ld-efm-decoder` tools also use `CMAKE_BUILD_TYPE=Debug`, but their inner
loops go through **Qt's pre-built `-O2` shared library** (`libQt6Core`):

- `QVector::append()` / `QVector::takeFirst()` → compiled code inside libQt6Core at
  `-O2`, using `memmove` for trivially-copyable element types.
- `orc`'s equivalent `std::vector::push_back()` / `std::vector::erase(begin())` →
  template code inlined and compiled at `-O0` per call site.

This means "both are Debug builds" is **not a fair comparison** — the old hot paths
ran through an optimised library, while the new hot paths are unoptimised inline templates.

### 3. `DelayLine::push()` uses O(N) shift instead of a ring buffer

```cpp
// orc (delay_lines.cpp) — O(N) shift on every single byte through CIRC:
m_buffer.erase(m_buffer.begin());   // shifts all N elements left
m_buffer.push_back(temp);
```

The CIRC decoder has delay lines up to **108 elements** long
(`m_delayLineM` in `dec_f2sectiontof1section.cpp`).  For each of the 28 × 28 = 784
delay-line pushes per F2 frame, the deepest lines shift ~54 elements on average.
At `-O0`, `vector::erase(begin())` does this via unoptimised iterator loops.

The old Qt code uses `QVector::takeFirst()` + `QVector::append()` which compile down
to a `memmove` of raw bytes inside the optimised Qt library.  Fix: replace
`std::vector` with a manually-tracked head-index ring buffer.

### 4. `-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE` — minor contributor

orc is compiled with `SPDLOG_ACTIVE_LEVEL=TRACE`, so every `ORC_LOG_TRACE` /
`ORC_LOG_DEBUG` macro expands to a runtime `should_log()` check even when the
runtime log level is `warn`.  Most of these calls are outside inner loops, so the
effect is small, but it adds function-call overhead on every decoder state transition.

---

## Observations (revised)

1. **The 4.8–4.9× wall-time slowdown is almost entirely explained by `-O0` STL
   overhead.** STL templates compiled without optimisation add ~20% direct CPU
   overhead and cause excessive heap allocation (malloc + free ≈ 3.75%) due to
   per-call vector copies that would be eliminated at `-O2`.

2. **The baseline comparison was unfair.** The standalone tools used Qt containers
   whose hot paths execute inside a pre-compiled `-O2` Qt library, while orc's hot
   paths are raw STL templates at `-O0`.

3. **`DelayLine::push()` has an O(N) regression** vs. the baseline's Qt implementation.
   Replacing the `erase(begin())`-based FIFO with a ring buffer is a correctness-neutral
   algorithmic improvement regardless of build type.

4. **cpu-bound, single-threaded, no cache pressure.** Cache miss rate (5.77%) and
   dTLB misses (16M) are modest; the problem is instruction volume, not memory bandwidth.

---

## Recommended fixes (priority order)

| # | Fix | Expected gain |
|---|---|---|
| 1 | Rebuild EFM decoder files with `-O2` via per-target `target_compile_options()` in CMake, even in Debug builds | Likely 4–8× speedup in EFM decode |
| 2 | Replace `DelayLine`'s `vector::erase(begin())` FIFO with a ring buffer | Removes O(N) inner-loop shift; meaningful gain at any -O level |
| 3 | Lower `SPDLOG_ACTIVE_LEVEL` to `SPDLOG_LEVEL_INFO` in Debug builds (or per-target) | Eliminates trace/debug runtime checks in hot paths |
| 4 | Re-run baseline comparison against a `RelWithDebInfo` build of both orc and standalone to get a fair apples-to-apples number | N/A (measurement only) |
