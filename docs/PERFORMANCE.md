# Performance and memory

How beta 11 compares with beta 10, how each figure was produced, and what
none of them is: **a measurement on a Flipper Zero**. Beta 11 is not
hardware tested; the device checks to run are in the
[hardware checklist](HARDWARE_CHECKLIST.md#memory-lifetimes-and-module-loading-beta-11).

Three kinds of figure appear here, and they are never mixed:

- **Binary footprint** — the code and static data the loader places in the
  heap, exact, from the built files (`scripts/fap_size.py`).
- **Device sizes** — the size of a heap object as the device compiler lays
  it out (`scripts/struct_sizes.py`, the SDK's own `arm-none-eabi-gcc`).
- **Simulated runtime heap** — the app's operations run on the host with
  every allocation going to a model of the firmware's heap allocator
  (`test/stubs/heap4.h`, a copy of the rules of `furi/core/memmgr_heap.c`:
  one arena, 8-byte headers and alignment, first fit by address, a block
  split only when more than 16 bytes remain, neighbours merged on free).
  The host has 64-bit pointers, so its objects are a little larger than a
  Flipper's (`RgAnalysis` 4,144 B against 4,072 B); compare the versions,
  not the absolute values.

## Binary footprint

| | Beta 10 | Beta 11 |
|---|---:|---:|
| Full edition, resident (Momentum) | 84,313 B | 84,433 B (+120 B) |
| Full edition, resident (Unleashed / RogueMaster) | 84,308 B / — | 84,428 B / 84,432 B |
| Catalog edition, resident (Official) | 86,513 B | 86,565 B (+52 B) |
| Largest module | 23,620 B (Find Similar) | 23,608 B (Find Similar) |
| Smallest module | 3,556 B (recovery) | 3,003 B (recovery) |
| Modules smaller than in beta 10 | — | 14 of 16 (Hopper +57 B, Waterfall +1 B) |
| `__udivmoddi4` (698 B) linked | resident app and 8 modules | nowhere |

Per module (Momentum build; Unleashed and RogueMaster within a few bytes):

| Module | Beta 10 | Beta 11 | Change |
|--------|-------:|-------:|-------:|
| `radiogeddon_about.fal` | 4,169 B | 3,561 B | −608 B |
| `radiogeddon_bits.fal` | 16,592 B | 16,170 B | −422 B |
| `radiogeddon_fp.fal` | 23,620 B | 23,608 B | −12 B |
| `radiogeddon_heatmap.fal` | 5,957 B | 5,365 B | −592 B |
| `radiogeddon_hopper.fal` | 7,259 B | 7,316 B | +57 B |
| `radiogeddon_lab.fal` | 18,505 B | 17,914 B | −591 B |
| `radiogeddon_log.fal` | 9,179 B | 8,577 B | −602 B |
| `radiogeddon_multi.fal` | 17,156 B | 16,747 B | −409 B |
| `radiogeddon_range.fal` | 21,745 B | 21,128 B | −617 B |
| `radiogeddon_recover.fal` | 3,556 B | 3,003 B | −553 B |
| `radiogeddon_reports.fal` | 18,385 B | 17,951 B | −434 B |
| `radiogeddon_sessions.fal` | 16,871 B | 16,627 B | −244 B |
| `radiogeddon_settings.fal` | 7,296 B | 6,452 B | −844 B |
| `radiogeddon_studio.fal` | 22,108 B | 21,711 B | −397 B |
| `radiogeddon_unknown.fal` | 18,784 B | 18,368 B | −416 B |
| `radiogeddon_wf.fal` | 4,496 B | 4,497 B | +1 B |

The resident app grew slightly: the module load-cost model and the
Database's protocol table are resident; removing the 64-bit division
helper and the separate RAW-counting load path paid for most of it.

## Runtime heap (device sizes)

| Object | Beta 10 | Beta 11 |
|--------|-------:|-------:|
| Analysis result `RgAnalysis` | 4,072 B | 4,072 B |
| Analyzer working state `RgAnalyzer` | 8,672 B (with the result inside) | 4,600 B (the result apart) |
| Analysis at its peak (both, with heap headers) | 8,680 B | 8,688 B |
| Bitstream Explorer, held while open | 8,672 B | 4,072 B (−4,600 B) |
| Database entry `RgDbEntry` | 40 B | 28 B |
| Find Similar, heap per tracked recording | 8 B | 4 B |

## Runtime heap (simulated)

`make -C test benchmem`; the same `test/bench_mem.c` built against the
beta 10 sources for the left column. "Held" is what stays allocated while
the screen is open; "frags" the free fragments then.

| Operation | Beta 10 peak / held / frags | Beta 11 peak / held / frags |
|-----------|---------------------------:|---------------------------:|
| Bitstream Explorer | 10,216 / 8,760 B / 2 | 10,232 / 4,152 B / 1 |
| Pulse Studio (open file + analysis, before its view) | 10,240 / 10,240 B / 1 | 10,256 / 5,632 B / 1 |
| Find Similar, 100 recordings (warm) | 1,504 / 0 B | 1,824 / 0 B |
| Find Similar, 1,000 recordings (warm) | 8,704 / 0 B | 5,536 / 0 B |
| Database, 100 files | 6,808 / 5,768 B / 2 | 6,400 / 5,368 B / 1 |
| Database, 1,000 files | 29,776 / 28,736 B, 500 indexed | 33,112 / 32,080 B, 662 indexed |

Find Similar at 100 recordings peaks 320 B higher: the handled-position
bitmap is a fixed 250 B. From a few hundred recordings on it is lower, by
7 KB at 2,000. The Database holds about 12 % more at 1,000 files because
it indexes 32 % more of them inside the same 32 KiB bound.

**Low memory** (100 recordings): the free block is cut in 512-byte steps
from 96 KiB to 1 KiB; at each of the 191 steps an operation completes or
refuses, no allocation may fail and a refusal must leave nothing allocated.
Both versions pass; the smallest free block each completes with is the
same in both (Bitstream 19,456 B, Pulse Studio 24,576 B, Find Similar
20,480 B; Database 26,624 B against 25,600 B, the extra being its work
buffers' allocation order).

**Long session**: 4,000 operations in random order between 64 blocks of the
rest of the app that come and go: 0 failed allocations, 0 B left
allocated, the largest free block back to 196,600 B, at most 20 free
fragments at once (beta 10: 21).

## Module loading

`scripts/module_cost.py` (and the same model in `helpers/rg_elf.c`, which the
app runs before every load):

| Module | Need beta 10 | Need beta 11 | Change |
|--------|-------:|-------:|-------:|
| About | 15,849 B | 12,040 B | −3,809 B |
| Bitstream Explorer | 41,770 B | 37,960 B | −3,810 B |
| Find Similar | 50,232 B | 46,424 B | −3,808 B |
| Activity Heatmap | 27,893 B | 24,088 B | −3,805 B |
| Frequency Hopper | 18,580 B | 16,056 B | −2,524 B |
| Dolphin Lab | 32,250 B | 28,440 B | −3,810 B |
| Activity Logger | 27,009 B | 23,200 B | −3,809 B |
| Multi-Capture Compare | 26,987 B | 25,056 B | −1,931 B |
| Range Scanner | 35,464 B | 32,896 B | −2,568 B |
| Recording recovery | 13,243 B | 10,320 B | −2,923 B |
| Reports | 42,527 B | 38,720 B | −3,807 B |
| Sessions | 26,867 B | 27,008 B | +141 B |
| Settings | 17,716 B | 14,784 B | −2,932 B |
| Pulse Studio | 52,431 B | 48,624 B | −3,807 B |
| Unknown Protocol report | 28,608 B | 26,648 B | −1,960 B |
| Waterfall view | 14,737 B | 11,920 B | −2,817 B |

"Need" is the free block asked for before loading, with the largest
screen's declared work. Beta 10: sections + 6 KB + work + 4 KB. Beta 11:
the loader's blocks for the module's sections and bookkeeping + 2 KB +
the larger of its relocation tables and its work + 4 KB. The 4 KB margin
is unchanged; the relocation tables (1–5.4 KB per module) are counted
from the file instead of guessed.

`make -C test benchmodules` replays each module's real load sequence and
its work on 4,000 randomly fragmented heaps:

| | Beta 10 rule | Beta 11 rule |
|---|---:|---:|
| Loads that fit, refused | 24,817 | 20,865 |
| Loads allowed that ran out of memory | 0 | 0 |

Both rules stay conservative: the firmware only reports its largest free
block, so a load that would fit across several smaller blocks is still
refused. Then 20,000 loads and unloads between the rest of the app's
blocks: 0 failed allocations, the heap whole again afterwards.

## Find Similar Signals

`make -C test benchfp` (stub card; card calls are what the device does,
times are the host's). "Lookups" are path lookups (stat, timestamp), each
of which scans the folder on a FAT card; "dir KiB*" models the directory
data those scans and the listings read (64 bytes a recording), an estimate.

| Recordings, search | Beta 10 listings / lookups / read KiB / dir KiB* / peak | Beta 11 |
|---|---|---|
| 500, cold | 9 / 1,000 / 1,286 / 25,252 / 19,186 B | 4 / 500 / 1,055 / 17,366 / 15,856 B |
| 500, warm | 1 / 1,000 / 190 / 15,734 / 4,654 B | 1 / 500 / 259 / 7,921 / 3,379 B |
| 1,000, cold | 17 / 2,000 / 2,573 / 100,761 / 23,186 B | 6 / 1,000 / 2,109 / 69,159 / 15,856 B |
| 1,000, warm | 1 / 2,000 / 379 / 62,718 / 8,654 B | 1 / 1,000 / 517 / 31,468 / 5,441 B |
| 2,000, cold | 33 / 4,000 / 5,147 / 402,546 / 31,186 B | 11 / 2,000 / 4,220 / 276,136 / 15,856 B |
| 2,000, warm | 1 / 4,000 / 760 / 250,437 / 16,654 B | 1 / 2,000 / 1,038 / 125,437 / 9,566 B |

A warm search reads more of the index file (each kept record is read back
to compare its name exactly) and makes half the path lookups. A RAW
recording is read four times while it is fingerprinted, not six.

## Database

`make -C test bench`: sorting, filtering, search and duplicate marking give
the same results as beta 10 (checksums equal at 10 to 4,682 entries); their
host times are within run-to-run noise of beta 10's. What changed is the
memory per entry and the number of files that fit (above).

## Reproducing

```bash
make -C test benchmem memstress benchfp bench
scripts/build_target.sh full-momentum out
python3 scripts/fap_size.py out/radiogeddon-full-momentum.fap
python3 scripts/module_cost.py .stage/full-momentum/dist
make -C test benchmodules FAL_DIR=../.stage/full-momentum/dist
UFBT_HOME=.ufbt-momentum python3 scripts/struct_sizes.py --root .stage/full-momentum
```

For the beta 10 column, check out `v1.0.0-beta.10`, copy beta 11's
`test/bench_mem.c`, `test/bench_fpindex.c` and `test/stubs/` into it, and
build them the same way; the benchmarks pick each version's API with its
feature macros.
