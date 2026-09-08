# Campaign machine record, 2026-09-08 evening

This is a dated record for the gate-1/2 campaign of the 2026-09-08
optimization-stack candidate. It does not rewrite the historical
`machine_record.md` (7.2A preparation), which described the powersave-era
controls. That record's limitation line ("governor cannot be changed: no root
in this environment") no longer applies: this session has passwordless sudo,
and the governor was pinned before any qualification capture below.

## Controls

| Control | Value |
|---|---|
| CPU model | AMD Ryzen 7 7700 8-Core Processor, 16 threads |
| Kernel | 7.2.3-1-cachyos |
| Pinned core | Logical CPU 7 via `taskset --cpu-list 7` on every run; single-threaded binaries |
| SMT sibling | Logical CPU 15 (thread siblings 7,15) taken `offline` before the first timed run and restored online after the last; recorded per pair below |
| Scaling driver | amd-pstate-epp |
| Scaling governor (CPU 7) | `performance` (pinned via sudo write before capture; previously `powersave`) |
| Energy performance preference (CPU 7) | `performance` |
| Turbo/boost policy | Global boost file `1` (enabled), fixed and untouched for the whole campaign |
| Frequency range | 3021308 to 5392872 kHz (3.02 to 5.39 GHz); governor holds the core near sustained maximum under load |
| Compiler | GCC 16.2.1 20260810 (`/usr/bin/g++`), Ninja 1.13.2 |
| Build preset | `linux-gcc-self-release`: `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_FLAGS=-O3 -march=native -flto=2`, Ninja generator, clean reconfigure and rebuild from committed HEAD `1b046db` |
| Candidate artifact | `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_value.candidate-2026-09-08`, sha256 `1dccaa808044593bd97d6e8af09848ce8b19e75b9dcefa50d63a17a6381db765`, read-only (444); the clean rebuild from HEAD reproduced the retained-session binary byte for byte |
| Baseline artifact | `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile.baseline`, sha256 `84cb7a309f59db98b33709ececc168d330991b2226956cc7b310445870aac376`, unchanged since 7.2A, read-only |
| Legacy comparator | `/home/icly/Documents/tetris_ai_runner_results/phase7/tetris_profile_legacy_cmp`, sha256 `08e91644054b5bc07da45462378596f3364ae7e58459edd59e5d8a38cf885450`, unchanged, read-only |
| Parameter file | `artifacts/frozen_29d.bin`, sha256 `ea95ba584f4eb5a7234bf0fbdd68fb422e00e29d13373e4df9e6b9ea2ca98037`, passed by absolute path |
| Environment | `TETRIS_AI_PARAM_FILE` explicitly unset on every run (`env -u`); no profile binary reads it |
| Competing work | No builds, tests, audits, or other commands run during the measurement windows; session desktop remains up (observed background load average about 1.8, no interaction) |
| Temperatures at capture start | Composite +40.9 C (CPU-adjacent sensor), +30.9 C (second composite); frequency at idle sample 4.28 GHz |

## Frequency-policy rationale

The powersave-era campaigns recorded plus or minus 2 percent unpinned-frequency
noise (historical record, Section 8 of the qualification protocol), and
tonight's powersave diagnostics showed the frozen baseline itself spreading
9.04 to 9.54 s across blocks. With the governor and EPP pinned to performance,
back-to-back identical runs agree within 0.7 percent and the baseline blocks
within about 1 percent. All ratios below therefore reflect code speed rather
than governor phase. Boost stays enabled and fixed so both sides of every pair
see identical frequency policy.

## Restoration

CPU 15 is returned online after the block. The measured offline window was
2026-09-08T16:12:03Z to 16:15:25Z, frequency sampled 5293670 kHz at block start
and 5282401 kHz at block end. The verdict for the campaign is recorded in
`results/phase7/gate12_2026-09-08/verdict.md`.
