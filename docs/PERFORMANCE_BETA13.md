# Performance and memory — beta 13 optimization pass

Beta 12 against beta 13, same compiler (`arm-none-eabi-gcc` 12.3.1 from the
pinned SDKs), same SDKs and firmware trees, same host. **Nothing here was
measured on a Flipper Zero when this was written.** The owner's later
session on RogueMaster `4f2e06200` read about 65.9 KB of loaded sections
and about 45 KB free after start (see
[VERIFICATION.md](VERIFICATION.md#reported-by-users-on-physical-hardware)). The
device checks are Y1–Y10 in the
[hardware checklist](HARDWARE_CHECKLIST.md#memory-and-relaunch-beta-13).

Every number has a label:

- **built** — exact, read from the built `.fap`/`.fal` files
  (`scripts/fap_size.py`, `readelf`);
- **device size** — the SDK compiler's `sizeof` (`scripts/struct_sizes.py`)
  or a size read in the firmware source;
- **device log** — from the owner's beta 12 run on RogueMaster `4f2e0620`
  (API 88.4), quoted in the task that started this release;
- **sim** — a replay on the host model of the firmware's allocator
  (`test/stubs/heap4.h`) with the built files (`test/bench_launch.c`,
  `test/bench_flows.c`, `test/bench_modules.c`);
- **host** — host tests (firmware code compiled on a PC where stated);
- **—** — not measured (needs a device; not estimated).

Beta 12 column: commit `5f4084e` (the v1.0.0-beta.12 release) built with
`scripts/build_target.sh` in a separate worktree. Beta 13 column: this
release's commit.

## Summary table

| Metric | Beta 12 | Beta 13 | Kind |
|---|---:|---:|---|
| Total resident footprint, RogueMaster `4f2e06200` device build | 84,812 B | **66,003 B (−18,809 B, −22.2 %)** | built |
| Total resident footprint, Unleashed / Momentum / RogueMaster pin | 84,808 / 84,812 / 84,812 B | 65,999 / 66,002 / 66,003 B | built |
| Total resident footprint, Catalog (Official) | 87,133 B | 87,967 B (+834 B) | built |
| Largest ELF allocation (Full, device build) | 39,828 B `.text` | **13,492 B `.text` (−66 %)** | built |
| Second-largest ELF allocation (Full, device build) | 31,148 B `.rgtext` | 11,496 B `.rgtext` | built |
| Largest ELF allocation (Catalog) | 43,704 B `.text` | 19,992 B `.rgtext` (−54 %) | built |
| Loader temporary memory: relocation tables (Full) | 23,764 B, largest 10,469 B | 20,629 B, largest 4,737 B | built |
| Loader peak, sections + tables (Full) | 108,576 B | 86,632 B (−21,944 B) | built |
| Launches that fit, 126,000 heaps after an app closed (device build) | 31,643 | **83,569** (none lost) | sim |
| Launches that fit, 24,000 randomly fragmented heaps (device build) | 735 | 6,861 (none lost) | sim |
| 60 launches in a row with services keeping blocks: launches before the first failure, mean | 6.8 | 35.3 | sim |
| Estimated runtime startup RAM | device log 14,848 B | ~13,950 B (the scanner view's ~0.9 KB is gone from start) | device log / device size |
| Free heap at app start, expected on the owner's device | device log 25,568 B | ~45,000 B (25,568 + 18,809 resident + ~0.9 KB view) | model |
| Receive session cost (RogueMaster) | device log 36,568 B; model 35,920 B | model 25,184 B (−10,736 B) | device log / sim |
| Receive preflight needs (cost + 6 KB margin) | 42,712 B (owner's device) | ~31.3 KB (model) | model |
| Receive sessions the preflight allowed that ran out (heap split by 0–4 small blocks, 18,000 heaps) | 298 | **0** | sim |
| Logger module after Adaptive Scanner → Back (2,000 × 4 rounds) | refused 3,288 of 8,000 | refused 0 of 8,000 | sim |
| Fragmented-heap module loads that fit (64,000 heaps, Unleashed modules) | 33,756 | 45,095 (19 modules) | sim |
| Module loads the check allowed that ran out | 0 | 0 | sim |
| Firmware decoders, 51 captures: identical decodes, receiver with only the fed decoders | — | 1,358 of 1,358 | host (firmware code) |
| Memory-safety findings (ASan/UBSan, TSan, libFuzzer, memstress) | 0 | 0 | host |

Beta 13 keeps every feature, all of beta 12's 16 modules, and adds three
(the Database, the analysis menu, the Scanner's screen): 19 modules,
module API 7.

## Where the 18.8 KB came from (built, Full edition)

| Change | Resident | Notes |
|---|---:|---|
| Signal Database list, options, file details, rename, list view → `radiogeddon_db.fal` | −10.7 KB | the index stays plain app data; the list's view exists only while shown and its place is kept in the index |
| Favorites, favorite options, frequency keyboard → settings module | −1.9 KB | favorites saved through the host table, so the module links no storage code |
| Analyze menu, Multi-Compare list and result, Smart Protocol Discovery, Decode → `radiogeddon_analyze.fal` | −2.8 KB | |
| Frequency Scanner screen and view → `radiogeddon_scan.fal` | −3.6 KB | the engine stays in the app (the Logger shares it) |
| Five code sections instead of two | +0.5 KB | functions in one named section cannot be dropped one by one; calls inside a section became direct branches |
| Tool forwarders in their section | −1.1 KB | `TOOL_SCENE` functions call their helpers directly |
| Settle/prep logic, decode-only receiver, preflight, host entries | +0.9 KB | |

Module sizes while their screen is shown (built, Unleashed): Database
12,594 B, analysis menu 4,295 B, Scanner screen 4,384 B, settings
9,080 B (beta 12: 6,396 B; it now also holds the favorites and keyboard).

## The section layout (built, sim)

The firmware's loader (`lib/flipper_application/elf/elf_file.c`, read in the
RogueMaster `4f2e06200` tree) gives every allocatable section and every
`.fast.rel.*` table a heap block of its own, checks that the largest free
block holds each one plus 1 KB, loads them all, relocates (freeing the
tables), then allocates the app thread's stack. Section names are free
(beta 12's `.rgtext` loaded on the device), so beta 13 cuts the app into
groups of whole source files (`radiogeddon_edition.h`, checked by
`scripts/text_sections.py`).

Four layouts were built from the same source and replayed on the same
heaps (Unleashed, before the Scanner module):

| Layout | Resident | Largest | Launches that fit after an app closed (of 126,000) | Random heaps (of 24,000) |
|---|---:|---:|---:|---:|
| beta 12 file | 84,808 | 39,824 | 31,643 | 735 |
| 2 sections (beta 12's layout, beta 13 code) | 68,973 | 34,372 | 65,765 | 1,720 |
| 3 sections (+ views) | 68,973 | 34,372 | 69,805 | 2,244 |
| 4 sections (+ radio helpers) | 69,359 | 19,772 | 76,245 | 4,939 |
| **5 sections (chosen)** | 69,511 | 14,888 | **77,111** | **5,683** |

None of the layouts loses a heap where beta 12 fits. The 5-section layout
is slightly worse than 4 on a few heaps with almost no free memory
(96 KB free, one stray block: 98.8 % against 99.2 %): five 1 KB checks
instead of four. A dead-code effect was found and avoided: a named
section keeps all of a file's functions, so `rg_ui.c` and `rg_journal.c`
(half their functions serve only modules) stay in `.text` (+1.66 KB
otherwise). The final device build is 66,003 B in `.text` 13,492,
`.rgtext` 11,496, `.rodata` 11,351, `.rgcore` 10,876, `.rglab` 10,108,
`.rgview` 8,424 B.

Catalog edition (all code resident, Official firmware): six code sections,
largest 19,992 B instead of 43,704 B; launches after an app closed 32,118
→ 43,571 of 126,000, but on 419 heaps with barely enough free memory only
beta 12 fits (+834 B of code and two more 1 KB checks). The Catalog
edition was not the target of this pass.

## The Logger after the Adaptive Scanner (device log, sim)

The device logged, on beta 12: module loaded 15,616 / 14,864 (free /
largest), scanner started 11,256 / 11,256, module unloaded 20,744 / 11,256,
scanner stopped 22,632 / 13,024, then `radiogeddon_log.fal needs 17088,
13024 free`. The scanner and its schedule (kept until the main menu) were
allocated while the setup screen's module was still loaded, so unloading
the module left a hole below them.

`test/bench_flows.c` replays it with the built `radiogeddon_log.fal` from
one free block of 25,568 B. The model gives 16,384 / 15,784, 11,824 /
11,672, 21,008 / 11,672, 22,912 / 13,216 and the same `needs 17088` — within
about 800 B of every device figure. In beta 13's order (the Scanner starts
from the event loop after unloading the module; the Logger and Hopper make
their engines before their module loads; Receive does the same) the
scanner lands where the module was and the free space stays whole: 22,912 /
22,912, and the module loads again.

## Receive (source, host, sim)

- The firmware receiver allocates a decoder for every registered protocol
  but feeds only those matching its filter (`lib/subghz/receiver.c`, all
  five trees). Under the app's `Decodable` filter, RAW and BinRAW are never
  fed, and BinRAW's decoder allocates 2,048 × 4 B + 2,048 B. The receiver
  now gets a registry of only the fed protocols, in the firmware's order:
  on the firmware's own 51 test captures, compiled on the host, all 1,358
  decodes are identical (protocol, text, hash, order) and 10,560 B fewer
  are allocated (host sizes; device: 10,240 B of buffers plus the object).
- A cost measured by beta 12 is still used, less exactly those buffers
  (`Radio_heap_model`), until a beta 13 session measures its own: the
  owner's 36,568 B becomes 26,328 B, so the first session is not refused on
  a stale figure.
- The worker's stack and stream buffer are now allocated first, and the
  preflight also requires the largest free block to hold
  `RG_MEM_SESSION_BLOCK` (16,640 B, the largest of the five firmwares).
  Beta 12's total-only rule let 298 of 18,000 split heaps start a session
  that then ran out (a crash on the device); beta 13's let none. The cost:
  on RogueMaster (8 KB buffer) some heaps with 8–16 KB largest blocks are
  refused although they would fit.

## Startup (device log, device size)

Beta 12 took 14,848 B between `launch` and `app start` on the device. The
largest single item is the firmware's Sub-GHz device registry, which loads
every radio driver plugin (`radio_device_cc1101_ext.fal`, 5,180 B of code,
built in the `4f2e06200` tree). Beta 13 creates the scanner view (728 B
snapshot model and its view) only on the Scanner screen. Deferring the
driver registry was investigated and not done: every radio screen needs
it, so it would only help non-radio tools, at the cost of loading and
unloading a plugin on every visit.

## Investigated, unchanged

- **The Dolphin Lab view as a module** (4 KB + drawing): it also shows
  cards on other screens, so it would be loaded and unloaded inside other
  tools' flows; not worth the risk without a device.
- **Short calls inside the app** (`-mlong-calls` is the SDK's): the
  section layout already makes calls inside a file direct; firmware calls
  must stay long (a trampoline otherwise, 20 B each).
- **A smaller Sub-GHz worker**: its buffer size is the firmware's; a
  smaller one could drop samples.
- **The module check's single-block rule**: with only the largest free
  block known (no heap walk is exported on RogueMaster 88.4), summing a
  module's blocks is the only safe bound; it is kept.

## Reproducing

```bash
git worktree add ../rg-b12 5f4084e && (cd ../rg-b12 && scripts/build_target.sh full-unleashed out)
scripts/build_target.sh full-roguemaster-device out    # and the other four targets
python3 scripts/fap_size.py out/*.fap
make -C test benchlaunch FAPS="b12=../rg-b12/out/radiogeddon-full-unleashed.fap b13=../out/radiogeddon-full-unleashed.fap"
make -C test benchflows FLOWS_NEW="../out/radiogeddon-full-unleashed.fap ../.stage/full-unleashed/dist" \
    FLOWS_OLD="../../rg-b12/out/radiogeddon-full-unleashed.fap ../../rg-b12/.stage/full-unleashed/dist"
make -C test benchmodules FAL_DIR=../.stage/full-unleashed/dist
make -C test check decoders formats captures lifecycle ui fpindex actlogfiles memstress tsan
make -C test fuzz FUZZ_CC=clang-18 FUZZ_TIME=60
python3 scripts/text_sections.py --check
PATH=$(ls -d .ufbt-unleashed/toolchain/*/bin):$PATH \
    python3 scripts/text_sections.py --elf .stage/full-unleashed/dist/debug/radiogeddon_full_d.elf
```
