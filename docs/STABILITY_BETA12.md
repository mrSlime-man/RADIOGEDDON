# Beta 12 stability: root causes, fixes and what still needs a device

This is the engineering record for the beta 12 stability pass, written for
the owner (who has the hardware) and for whoever integrates beta 12. It keeps
four kinds of statement apart, and every claim below is labelled with one:

- **[source]** read in the firmware's or the app's source code;
- **[host]** reproduced or measured on a PC (unit tests, sanitizers, the
  heap model of the firmware allocator);
- **[sim]** a simulation (the heap model, the loader replay);
- **[device]** observed on a Flipper Zero. The only device evidence so far is
  the owner's report on beta 11 (below). **Nothing in beta 12 has been run on
  a device yet: every build is NOT HARDWARE TESTED.**

Figures and benchmarks are in [PERFORMANCE_BETA12.md](PERFORMANCE_BETA12.md);
the device checks to run are in the
[hardware checklist](HARDWARE_CHECKLIST.md#crash-and-relaunch-fixes-beta-12).

## What was reported [device]

Beta 11, Full edition, on RogueMaster. The device's *Firmware Info* screen
reads `rm-420 [01-10-2026]`, `4f2e0620 [88.4] 1.20.0:L`.

- Launch, Receive, the Frequency Scanner, the Database and the menus work.
- **Scan → Adaptive Scanner → Start scanner stops the device with
  `furi_check failed` and it reboots.**
- **Leaving the app and opening it again about a second later sometimes
  fails for lack of RAM.**

## 1. The Adaptive Scanner crash — root cause found

**Cause [source].** Every firmware the app supports checks the callback
passed to `variable_item_list_set_enter_callback()`:

```c
void variable_item_list_set_enter_callback(VariableItemList* list,
    VariableItemListEnterCallback callback, void* context) {
    furi_check(callback);
    ...
```

(RogueMaster `4f2e06200` and `38d7ae9`, Official 1.4.3, Momentum mntm-012 and
Unleashed unlshd-093 all have it: `applications/services/gui/modules/variable_item_list.c`.)
Beta 11's Adaptive Scanner setup screen cleared that callback **with NULL**
in its exit handler. *Start scanner* leaves that screen for the Scanner, so
the exit handler ran and the firmware stopped with `furi_check failed`,
before the scanner itself ever started. Back from the same screen did the
same.

**Why the message matches [source].** A `furi_check()` without a message
stores the flag `__FURI_CHECK_MESSAGE_FLAG`; `__furi_crash_implementation`
(`furi/core/check.c`) turns it into the text `furi_check failed`, keeps it
for the next boot (`furi_hal_rtc_set_fault_data`) and resets. An
out-of-memory stop would read `out of memory` (`memmgr_heap.c`:
`furi_check(pvReturn, "out of memory")`), a stack overflow or hard fault
their own texts. So the report rules out memory exhaustion for this crash.

**Same bug elsewhere [source].** Two other screens did the same: the
**RF Activity Logger** (any exit) and **Database options** (any exit — in
the Catalog edition too, on Official firmware). Neither had been tried on
the device. A static check of every firmware argument contract (below)
finds exactly these three calls in beta 11 and nothing else.

**A second, latent hazard [source].** The shared list keeps the last
callback it was given. Screens that set none (Customize, the scan list)
inherited the previous screen's — and in the Full edition that screen's
code can be a module that was already unloaded. Pressing OK there would
call freed code.

**Fix.** `radiogeddon_scene_var_list_release()` empties the list and gives
it a resident do-nothing callback. It is installed at startup, called by
every list screen when it closes and by the list screens that have no
enter action of their own when they open. Modules reach it through the host
table (module API 6, so a stale module from beta 11 is refused, not
called).

**Confidence.** The crash site is identified from source with no remaining
alternative on this path: the scanner thread, the adaptive schedule and the
view were also reviewed (no firmware check on that path that its
arguments can fail; the schedule's allocation is 1,072 B). It is *not yet confirmed on the device*; checklist
item X1–X4 confirms it.

**Prevention.** `scripts/check_fw_contracts.py` derives from the firmware
sources which arguments each API function `furi_check()`s (810 functions,
union of the five trees above) and fails CI on any call passing a literal
NULL there; its self-test proves it still finds the beta 11 pattern.

## 2. Relaunch fails for lack of RAM — cause and mitigation

**How the loader allocates [source].** `lib/flipper_application/elf/elf_file.c`
gives every allocatable ELF section its own heap block, and refuses the
section (the launch fails with a not-enough-memory error) when the largest
free block is smaller than the section plus 1 KB (`elf_load_section_data`).
Beta 11's resident code was **one 70,940-byte `.text` section**, so a launch
needed **one free block of about 72 KB**, plus 13 KB for `.rodata`.

**Why "right after closing" [source, sim].** After an app exits, firmware
services allocate again in the space it left (the desktop animation is
restarted, the menu or archive that relaunches it runs, storage and timers
allocate), first fit from the lowest address. A handful of small blocks left
in the freed region is enough to cut it below 72 KB although plenty of heap
is free in total. That matches "sometimes, about a second after closing".

**What we could not prove [—].** Whether the device also *leaked* memory
across a run. The beta 11 sources show no leaked object on the paths
reviewed (every allocation in `radiogeddon_app_alloc` has its free, every
record opened is closed, all four engine threads are joined before their
objects are freed, the host lifecycle tests leave 0 bytes), but only the
device can tell; checklist X7 does it with the firmware's own heap trace.

**Mitigation [source, sim].** The screens' code (`scenes/`, `views/`) is now
in a second code section, `.rgtext` (`RG_UI_TEXT` in `radiogeddon_edition.h`,
ARM builds only). The loader treats it like any other section; the build
tools keep it with its own relocation tables (checked with `readelf` on
every built file: `.rgtext` + `.fast.rel.rgtext`).

| Unleashed Full build | Beta 11 | Beta 12 |
|---|---:|---:|
| Largest block the loader needs | 70,908 B (`.text`) | 39,824 B (`.text`) |
| Second code block | — | 31,148 B (`.rgtext`) |
| All allocatable sections | 84,428 B | 84,808 B |

On the loader replay over a model heap where an app just closed and a few
small blocks were left behind, the share of launches that fit rises sharply
(120 KB free with two stray blocks: 32.5 % → 90.0 %); over 98,000 such heaps,
410 (0.4 %) fit only with the old layout: with barely enough free heap in
total (112 KB), two sections need two 1 KB checks and beta 11 fits more
often (see PERFORMANCE_BETA12).
This is a simulation; checklist X8–X9 is the device test.

**Also new.** The Scanner screen checks the largest free block before it
allocates the scanner, its adaptive schedule and its thread (firmware
`malloc()` never returns NULL — it stops the device) and shows
`Not enough memory` instead.

## 3. RogueMaster version on the device [source]

The device runs RogueMaster **`4f2e06200`** (2026-10-01, API **88.4**).
Beta 11's RogueMaster build was made in the tree of **`38d7ae9`**
(2026-10-08, API **88.16**). The RogueMaster loader compares only the API
*major* version (`application_manifest.c`: the minor comparison is
commented out), and every one of the app's 319 imports exists in 88.4, so
the 88.16 build loads without any warning. The headers the app compiles
against differ between the two commits only in places it does not use
(`canvas_set_orientation` moved to `canvas.h`, `view_dispatcher_show_loading`
added, protocol internals, a plugin-manager prefix filter), so the version
gap is **not** a cause of the crash. To be exact anyway, beta 12 adds a
device build:

```bash
RM_REF=4f2e06200 RM_API=88.4 scripts/build_target.sh full-roguemaster
```

which builds in that commit's tree, asserts its API and records it as a
device build (not a release artifact). To find your own: *Settings → About →
Firmware Info*, second line: `<commit> [<API>]`.

## 4. Lifetimes that were checked [source, host]

| Object | Allocated / freed by | Thread | Note |
|---|---|---|---|
| Shared GUI modules (submenu, widget, list, popup, text input, text box, scanner and receiver views) | app start / app exit | GUI | reset by each screen's exit; list now never holds a NULL or module callback |
| Optional views (Database, Timeline, Spectrum, Waterfall, Bits, Room, Studio, Heatmap, Lab, number input) | the screen's enter / its exit | GUI | each switch to one follows its `add_view` in the same screen (checked screen by screen) |
| Screen modules (16 `.fal`) | first screen of the module / the tick after its last screen closed | GUI | never unloaded while its code is on the stack |
| Scanner (+ 1,072 B adaptive schedule, 1.5 KB thread) | Scanner or Logger screen / app exit; thread: start / stop (joined) | GUI + sweep thread | hit callback and observer cleared by the Logger before its module unloads |
| Hopper engine | Hopper screen / app exit | GUI + hopper thread | **beta 12:** its callback (module code) cleared when the screen closes |
| Range Scanner engine | its screen / its screen's exit (asserted at app exit) | GUI + scan thread | freed before the module unloads |
| Receive session (decoders, keystore, worker) | Receive / Stop | GUI + worker | only while receiving |
| Popup timeout | screen | timer → GUI | `popup_reset` clears the callback, so a late timeout calls nothing |

## 5. Diagnostics for the device

The app writes one `RG.mem` log line at each lifecycle step: `launch`,
`app start`, every module load and `module unloaded`, `Scanner started` /
`Adaptive Scanner started`, `Scanner stopped`, `closing`, `exit`. Each gives
the free heap, the **largest free block** and the lowest free heap since
boot; at exit it also says how much heap was not returned since launch.

To capture (qFlipper or any serial terminal, 230400 baud):

```
log info          # in the Flipper CLI, before opening the app
```

For an exact leak count, turn on *Settings → System → Heap Trace* (`Main`)
before launching: on exit the firmware logs `radiogeddon_full allocation
balance: N` (0 = nothing leaked by the app's thread). After closing, the CLI
commands `free` and `free_blocks` show whether the free space is whole or in
pieces.

If the device still stops, the next boot shows the crash text; with
`log debug` running the console also prints the registers, the thread name
and the heap at the moment of the crash. For a call stack, the official
[debugging guide](https://developer.flipper.net/flipperzero/doxygen/app_dev_debugging_faps.html)
attaches a debugger over SWD (a Wi-Fi devboard or ST-Link) and `fbt debug`
loads the app's debug symbols (`radiogeddon_full_d.elf` from the build).

## 6. What remains unverified

- Every item above on a device (checklist X1–X10).
- Whether the relaunch failure was fragmentation alone: the `RG.mem` lines
  and the heap-trace balance will say.
- Peak RAM of the heavy tools on RogueMaster `4f2e06200` specifically (the
  heap model uses the firmware allocator's rules but not RogueMaster's
  background services).
