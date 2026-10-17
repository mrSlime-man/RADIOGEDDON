# Performance and memory — beta 12 stability pass

Beta 11 against beta 12, same compiler, same SDKs, same host. **Nothing here
was measured on a Flipper Zero; every build is NOT HARDWARE TESTED.** The
device checks are X1–X10 in the
[hardware checklist](HARDWARE_CHECKLIST.md#crash-and-relaunch-fixes-beta-12);
the root-cause analysis is in [STABILITY_BETA12.md](STABILITY_BETA12.md).

Labels: **built** — exact, from the built files (`scripts/fap_size.py`,
`readelf`); **device size** — the SDK's `arm-none-eabi-gcc`
(`scripts/struct_sizes.py`); **sim** — host replay on the model of the
firmware's heap allocator (`test/stubs/heap4.h`); **host** — host tests;
**—** — not measured (needs a device; not estimated here).

Beta 11 column: tag `v1.0.0-beta.11` built with `scripts/build_target.sh`
in a separate worktree. Beta 12 column: this branch.

## Summary table

| Measurement | Beta 11 | Beta 12 | Difference | Kind |
|---|---:|---:|---:|---|
| Full resident footprint (Unleashed) | 84,428 B | 84,808 B | +380 B | built |
| Full resident footprint (Momentum / RogueMaster pin) | 84,433 / 84,432 B | 84,812 / 84,812 B | +379 / +380 B | built |
| Catalog resident footprint (Official) | 86,564 B | 87,135 B | +571 B | built |
| **Largest block a launch needs (Full, Unleashed)** | **70,908 B** | **39,824 B** | **−31,084 B (−44 %)** | built |
| Largest block a launch needs (Catalog, Official) | 70,260 B | 43,704 B | −26,556 B (−38 %) | built |
| Launches that fit, 98,000 heaps after an app closed | 19,084 | 29,762 | +56 % | sim |
| Launches that fit, 18,000 randomly fragmented heaps | 192 | 735 | ×3.8 | sim |
| Module loads that fit, 64,000 fragmented heaps | 28,615 | 33,756 | +18 % | sim |
| Module loads allowed that then ran out of memory | 0 | 0 | — | sim |
| Adaptive Scanner start: schedule + thread (device) | 1,072 B + 1,536 B stack | same | 0 | device size |
| Adaptive Scanner start: crash on Start | yes (`furi_check failed`) | fixed in source | — | device (beta 11) / source |
| Startup peak RAM, largest free block after init | — | — | — | needs device (`RG.mem` lines) |
| Receive-session memory | — | — | — | needs device (logged per session) |
| Bitstream / Pulse Studio / Find Similar peak | unchanged code | unchanged code | 0 | see [PERFORMANCE.md](PERFORMANCE.md) |
| Database memory per file | 28 B | 28 B | 0 | device size |
| Lifecycle leaks (engine suites; beta 12 adds 1,200 Adaptive runs) | 0 B | 0 B | 0 | host |
| Allocation failures in the memory stress suites | 0 | 0 | 0 | sim |
| Calls breaking a firmware `furi_check` contract | 3 | 0 | −3 | source (CI) |

The resident footprint grew by 380 B: the memory checkpoints, the
Scanner's pre-start check, the resident list callback and the second
section's alignment. What changed is not how much RAM the code takes but
**how it is cut**: two blocks of 39.8 KB and 31.1 KB instead of one of
70.9 KB.

Beta 12 does not change the analysis, database, fingerprint or studio code
paths, so their peak and retained heap are beta 11's (measured in
[PERFORMANCE.md](PERFORMANCE.md)); CPU time and SD-card operations of those
paths are likewise unchanged and were not re-benchmarked.

## Sections of each build (built)

| Build | `.text` | `.rgtext` | `.rodata` | data + bss |
|---|---:|---:|---:|---:|
| Beta 11 Full (Unleashed) | 70,908 B | — | 13,264 B | 256 B |
| Beta 11 Catalog (Official) | 70,260 B | — | 15,804 B | 500 B |
| Beta 12 Full (Unleashed) | 39,824 B | 31,148 B | 13,580 B | 256 B |
| Beta 12 Full (Momentum, RogueMaster) | 39,828 B | 31,148 B | 13,580 B | 256 B |
| Beta 12 Catalog (Official) | 43,704 B | 26,840 B | 16,083 B | 508 B |

Modules (Unleashed, beta 12) split the same way; the largest blocks now are
Find Similar's `.text` 19.6 KB, Unknown Protocol 14.8 KB and Multi-Compare
14.2 KB (these keep their engines in `.text`), Pulse Studio 11.6 KB +
8.1 KB, Range Scanner 8.9 KB + 10 KB, Dolphin Lab 4.2 KB + 9.8 KB.

## The launch replay (sim)

`make -C test benchlaunch FAP_A=<beta 11 .fap> FAP_B=<beta 12 .fap>`
replays the firmware loader's allocations (`elf_file.c`: a block per
allocatable section behind a "largest free block ≥ size + 1 KB" check, the
relocation tables, the app thread's 4 KB stack) for both files on the same
heaps of a 180 KB arena.

**After an app closed** — the firmware's blocks below, the rest free except
K small blocks left by services in it — percent of heaps where the launch
fits, beta 11 / beta 12:

| Free | K=0 | K=1 | K=2 | K=3 | K=4 | K=5 | K=6 |
|---|---|---|---|---|---|---|---|
| 104 KB | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 112 KB | 100 / 100 | **15.2 / 0.0** | 0.3 / 0.0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| 120 KB | 100 / 100 | 77.0 / 100 | 32.5 / 90.0 | 9.8 / 41.6 | 1.6 / 10.4 | 0.3 / 1.2 | 0.1 / 0.2 |
| 128 KB | 100 / 100 | 89.8 / 100 | 53.6 / 99.5 | 25.4 / 87.0 | 9.2 / 57.1 | 3.6 / 28.4 | 1.0 / 10.3 |
| 136 KB | 100 / 100 | 95.4 / 100 | 68.8 / 100 | 39.0 / 94.5 | 19.6 / 79.0 | 8.9 / 55.6 | 3.1 / 33.3 |

The trade-off is visible in the 112 KB row: when there is only just enough
free heap in total, two sections each need their own 1 KB of slack and the
380 B of new code count, so beta 11 fits on 15 % of those heaps and beta 12
on none (410 of the 98,000 heaps fit only beta 11). From 120 KB free on,
beta 12 fits far more often. This is a model of the allocator, not of
RogueMaster's services; X8–X9 on the device decide.

## Reproducing

```bash
scripts/build_target.sh full-unleashed out          # and the other targets
python3 scripts/fap_size.py out/radiogeddon-full-unleashed.fap
git worktree add ../rg-b11 v1.0.0-beta.11 && (cd ../rg-b11 && scripts/build_target.sh full-unleashed out)
make -C test benchlaunch FAP_A=../rg-b11/out/radiogeddon-full-unleashed.fap \
    FAP_B=out/radiogeddon-full-unleashed.fap
make -C test benchmodules FAL_DIR=<.fal folder of each build>
make -C test check lifecycle tsan memstress ui
python3 scripts/check_fw_contracts.py --self-test && python3 scripts/check_fw_contracts.py
UFBT_HOME=.ufbt-unleashed python3 scripts/struct_sizes.py --root .stage/full-unleashed RgAdapt
```
