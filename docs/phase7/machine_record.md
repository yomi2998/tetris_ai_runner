# Machine-control record (7.2A preparation)

Recorded before any qualification capture. Any control this machine cannot
set is disclosed rather than omitted, per plan Section 17.3 item 10.

| Control | Value |
|---|---|
| CPU model | AMD Ryzen 7 7700 8-Core Processor, 16 threads |
| Kernel | 7.2.3-1-cachyos |
| Pinned core | Physical core 7 via `taskset --cpu-list 7` on every timing run |
| Worker count | 1 (all profile binaries are single-threaded) |
| Compiler | GCC 16.2.1 20260810 (`/usr/bin/gcc`, `/usr/bin/g++`) |
| Preset | `linux-gcc-self-release`: `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS=-O3 -march=native -flto=2`, Ninja generator, clean configure and build |
| Link flags | Same preset for all artifacts (LTO links the profile binaries) |
| Scaling governor | `powersave` (cannot be changed: no root in this environment) |
| Turbo/boost | Boost file reports enabled (`1`); cannot be changed without root |
| Frequency range | CPU min 422 MHz, max 5392 MHz (observed scaling 76 percent at record time; frequency is not pinned) |
| Competing workloads | No builds, reviews, or review workloads run during measurement windows (operator discipline; build trees are idle) |
| Environment | `TETRIS_AI_PARAM_FILE` explicitly unset on every run command; no profile binary reads it (verified by source inspection) |

## Limitations affecting interpretation

- Without governor, frequency, and boost control, absolute times carry
  machine drift (observed upward drift across long windows). All binding
  comparisons use within-pair ratios from immediately adjacent runs, which
  cancel first-order drift; the telemetry pre-gate below still failed by a
  margin far beyond drift.
- Single pinned core plus single-threaded binaries leaves 15 cores idle;
  thermal headroom is ample but frequency still varies under `powersave`.
