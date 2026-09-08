# Campaign machine record, gate 1 and 2 rerun, 2026-09-09

## Revision and tree

HEAD: 280c140b7c9a0ad9e7ca8b81aaf653207caff973
git status --short showed only untracked files (docs research files, results/phase7/_pipeline/, results/phase7/gate12_2026-09-09/). No tracked modifications.

## Hardware and software

CPU model: AMD Ryzen 7 7700 8-Core Processor
Kernel: 7.2.3-1-cachyos
Compiler: gcc (GCC) 16.2.1 20260810

## CPU controls

CPU 7 pin: every timed run and every pre-warm run used taskset --cpu-list 7.
Governor before: performance. Command: cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor. No change needed.
EPP before: performance. Command: cat /sys/devices/system/cpu/cpu7/cpufreq/energy_performance_preference. No change needed.
Boost: 1, fixed. Command: cat /sys/devices/system/cpu/cpufreq/boost. Not changed.
CPU 15 offline window: 2026-09-08T16:44:10Z to 2026-09-08T16:47:28Z. Commands: echo 0 and echo 1 piped through sudo tee to /sys/devices/system/cpu/cpu15/online. Verified online after the block with cat /sys/devices/system/cpu/cpu15/online reading 1.
CPU 7 frequency at block start: 5324840 kHz. At block end: 5259751 kHz. Command: cat /sys/devices/system/cpu/cpu7/cpufreq/scaling_cur_freq.

## Load samples

Pre-block quiet check: /proc/loadavg read 0.89 1.00 1.23, so 5-minute field 1.00 below 1.5 on the first sample. No wait loop needed.
Per-run load1 and load5 recorded in MANIFEST.txt controls fields:
run 1: 0.64 / 0.93
run 2: 0.88 / 0.97
run 3: 0.91 / 0.97
run 4: 1.09 / 1.01
run 5: 1.20 / 1.04
run 6: 1.22 / 1.05
run 7: 1.16 / 1.05
run 8: 1.12 / 1.04
run 9: 1.23 / 1.07
run 10: 1.36 / 1.12
No mid-block sample reached the 2.0 abort bar. No pair wait was needed.

## Ambient notes

Desktop session running with background load below 1.5 throughout. No builds, tests, or other heavy work were run concurrently with the timed block.

## Pre-warm procedure

Both binaries were pre-warmed immediately before pair 1.1-a with --moves 40 --iters 200 --telemetry off, outputs discarded, per runbook section 2.
Baseline pre-warm completed 2026-09-08T16:44:11Z. Candidate pre-warm completed 2026-09-08T16:44:12Z. Pair 1.1-a started 2026-09-08T16:44:12Z with no idle gap.
