# Host-side unit tests for RadioGeddon's firmware-independent logic.
# Built and run with the host compiler (no Flipper SDK required).
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -I..
# Sanitizers catch out-of-bounds / UB in the parsing + analysis code.
SAN ?= -fsanitize=address,undefined -fno-sanitize-recover=all

BUILD := build
DSP_SRC := test_dsp.c ../helpers/radiogeddon_dsp.c
ANALYZER_SRC := test_analyzer.c ../helpers/rg_analyzer.c
SCAN_SRC := test_scan.c ../helpers/rg_scan.c
HOP_SRC := test_hop.c ../helpers/rg_hop.c ../helpers/rg_scan.c
RAW_SRC := test_raw.c ../helpers/rg_raw.c ../helpers/rg_analyzer.c
TIMELINE_SRC := test_timeline.c ../helpers/rg_timeline.c
RING_SRC := test_ring.c ../helpers/rg_ring.c
RAWFMT_SRC := test_rawfmt.c ../helpers/rg_rawfmt.c ../helpers/rg_raw.c
DB_SRC := test_db.c ../helpers/rg_db.c
FREQ_SRC := test_freq.c ../helpers/rg_freq.c
PRESET_SRC := test_preset.c ../helpers/rg_preset.c
MEMSTAT_SRC := test_memstat.c ../helpers/rg_memstat.c
DECODE_SRC := test_decode.c ../helpers/rg_decode.c ../helpers/rg_raw.c
DBLOAD_SRC := test_dbload.c stubs/furi_stub.c ../helpers/radiogeddon_db.c ../helpers/rg_db.c
STUB_HDRS := stubs/furi.h stubs/storage/storage.h stubs/datetime/datetime.h \
		stubs/lib/toolbox/stream/stream.h stubs/core/check.h stubs/core/common_defines.h \
		stubs/furi_hal_rtc.h stubs/lib/subghz/devices/preset.h
RECORDER_SRC := test_recorder.c stubs/furi_stub.c ../helpers/radiogeddon_recorder.c \
		../helpers/rg_ring.c ../helpers/rg_rawfmt.c ../helpers/rg_raw.c

# Format tests: the firmware's own FlipperFormat and stream code and its
# Sub-GHz test files, downloaded at the pinned release by firmware/fetch.sh
# (checked against firmware/files.sha256, never committed). `make formats`.
FW := $(BUILD)/fw
FW_SRC := $(FW)/lib/flipper_format/flipper_format.c $(FW)/lib/flipper_format/flipper_format_stream.c \
		$(FW)/lib/toolbox/stream/stream.c $(FW)/lib/toolbox/stream/string_stream.c \
		$(FW)/lib/toolbox/stream/file_stream.c $(FW)/lib/toolbox/stream/buffered_file_stream.c \
		$(FW)/lib/toolbox/stream/stream_cache.c $(FW)/lib/toolbox/hex.c $(FW)/lib/toolbox/strint.c
FW_OBJ := $(patsubst $(FW)/%.c,$(BUILD)/fwobj/%.o,$(FW_SRC))
FORMATS_SRC := test_formats.c stubs/furi_stub.c ../helpers/radiogeddon_storage.c \
		../helpers/radiogeddon_settings.c ../helpers/radiogeddon_bands.c ../helpers/rg_freq.c \
		../helpers/radiogeddon_dsp.c \
		../helpers/rg_db.c ../helpers/rg_raw.c ../helpers/rg_analyzer.c

# Decoder tests: the firmware's own Sub-GHz receiver and protocol decoders
# (lib/subghz, every protocol it registers) run over its own RAW test captures
# through rg_decode, fetched like the format tests. `make decoders`.
FW_FILES := $(shell awk '{print $$2}' firmware/files.sha256)
SUBGHZ_SRC := $(addprefix $(FW)/,$(filter-out lib/subghz/devices/%,$(filter lib/subghz/%.c \
		lib/toolbox/manchester_%.c lib/toolbox/float_tools.c,$(FW_FILES))))
DEC_FW_SRC := $(FW_SRC) $(SUBGHZ_SRC)
DEC_FW_OBJ := $(patsubst $(FW)/%.c,$(BUILD)/fwdec/%.o,$(DEC_FW_SRC))
DEC_STUB_HDRS := $(STUB_HDRS) stubs/furi_hal.h
# The firmware's built-in radio presets and example settings file, through the
# custom preset check (test_fwpreset; also run by `make decoders`).
FWPRESET_SRC := test_fwpreset.c ../helpers/rg_preset.c
FWDECODE_SRC := test_fwdecode.c stubs/furi_stub.c stubs/subghz_stub.c ../helpers/rg_decode.c \
		../helpers/rg_raw.c ../helpers/radiogeddon_decode_text.c ../helpers/radiogeddon_bands.c

# Analyzer tests on real captures: rg_analyzer over the firmware's RAW test
# captures, scored against each protocol's timing in its decoder source
# (both fetched like the decoder tests; pure C, nothing of the firmware is
# compiled). `make captures`.
FWANALYZE_SRC := test_fwanalyze.c ../helpers/rg_analyzer.c ../helpers/rg_raw.c

# Fuzz targets (test/fuzz/). `check` replays their committed corpus with the
# host compiler; `make fuzz` runs them under libFuzzer (needs clang).
FUZZ_RAW_SRC := fuzz/fuzz_raw.c ../helpers/rg_raw.c ../helpers/rg_analyzer.c
FUZZ_DB_SRC := fuzz/fuzz_db.c ../helpers/rg_db.c ../helpers/radiogeddon_dsp.c
FUZZ_SAMPLES_SRC := fuzz/fuzz_samples.c ../helpers/rg_analyzer.c ../helpers/rg_timeline.c
FUZZ_TARGETS := fuzz_raw fuzz_db fuzz_samples
FUZZ_CC ?= clang
FUZZ_TIME ?= 60

.PHONY: check clean tsan fuzz formats decoders captures
check: $(BUILD)/test_dsp $(BUILD)/test_analyzer $(BUILD)/test_scan $(BUILD)/test_hop $(BUILD)/test_raw \
		$(BUILD)/test_timeline $(BUILD)/test_ring $(BUILD)/test_rawfmt $(BUILD)/test_recorder \
		$(BUILD)/test_db $(BUILD)/test_memstat $(BUILD)/test_dbload $(BUILD)/test_decode $(BUILD)/test_freq $(BUILD)/test_preset \
		$(addprefix $(BUILD)/replay_,$(FUZZ_TARGETS))
	$(BUILD)/test_dsp
	$(BUILD)/test_analyzer
	$(BUILD)/test_scan
	$(BUILD)/test_hop
	$(BUILD)/test_raw
	$(BUILD)/test_timeline
	$(BUILD)/test_ring
	$(BUILD)/test_rawfmt
	$(BUILD)/test_recorder
	$(BUILD)/test_db
	$(BUILD)/test_memstat
	$(BUILD)/test_dbload
	$(BUILD)/test_decode
	$(BUILD)/test_freq
	$(BUILD)/test_preset
	for t in $(FUZZ_TARGETS); do $(BUILD)/replay_$$t fuzz/corpus/$$t || exit 1; done

$(BUILD)/test_dsp: $(DSP_SRC) ../helpers/radiogeddon_dsp.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(DSP_SRC)

$(BUILD)/test_analyzer: $(ANALYZER_SRC) ../helpers/rg_analyzer.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(ANALYZER_SRC)

$(BUILD)/test_scan: $(SCAN_SRC) ../helpers/rg_scan.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(SCAN_SRC)

$(BUILD)/test_hop: $(HOP_SRC) ../helpers/rg_hop.h ../helpers/rg_scan.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(HOP_SRC)

$(BUILD)/test_raw: $(RAW_SRC) ../helpers/rg_raw.h ../helpers/rg_analyzer.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(RAW_SRC)

$(BUILD)/test_timeline: $(TIMELINE_SRC) ../helpers/rg_timeline.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(TIMELINE_SRC)

$(BUILD)/test_ring: $(RING_SRC) ../helpers/rg_ring.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -pthread -o $@ $(RING_SRC)

$(BUILD)/test_rawfmt: $(RAWFMT_SRC) ../helpers/rg_rawfmt.h ../helpers/rg_raw.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(RAWFMT_SRC)

$(BUILD)/test_db: $(DB_SRC) ../helpers/rg_db.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(DB_SRC)

$(BUILD)/test_memstat: $(MEMSTAT_SRC) ../helpers/rg_memstat.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(MEMSTAT_SRC)

$(BUILD)/test_freq: $(FREQ_SRC) ../helpers/rg_freq.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(FREQ_SRC)

$(BUILD)/test_preset: $(PRESET_SRC) ../helpers/rg_preset.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(PRESET_SRC)

$(BUILD)/test_decode: $(DECODE_SRC) ../helpers/rg_decode.h ../helpers/rg_raw.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(DECODE_SRC)

# The recorder and the Database loader are device code: they build against
# small Furi/Storage stand-ins that also count their allocations.
$(BUILD)/test_recorder: $(RECORDER_SRC) ../helpers/radiogeddon_recorder.h $(STUB_HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -Istubs -DSTUB_TRACK_ALLOC -pthread -o $@ $(RECORDER_SRC)

$(BUILD)/test_dbload: $(DBLOAD_SRC) ../helpers/radiogeddon_db.h ../helpers/rg_db.h $(STUB_HDRS) \
		| $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -Istubs -DSTUB_TRACK_ALLOC -pthread -o $@ $(DBLOAD_SRC)

formats: $(BUILD)/test_formats
	$(BUILD)/test_formats

$(FW)/.fetched: firmware/files.sha256 firmware/fetch.sh ../scripts/firmware_pins.sh
	firmware/fetch.sh $(FW)
	touch $@

$(FW_SRC): $(FW)/.fetched ;

# The firmware's code is built as it is, without the allocation counter's
# macros (they would rename its vtable->free member); LeakSanitizer still
# checks it, and the counter covers RadioGeddon's code and the strings.
$(BUILD)/fwobj/%.o: $(FW)/%.c $(STUB_HDRS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SAN) -D_GNU_SOURCE -I$(FW) -I$(FW)/lib -Istubs -c -o $@ $<

$(BUILD)/test_formats: $(FORMATS_SRC) $(STUB_HDRS) $(FW_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -D_GNU_SOURCE -I$(FW) -I$(FW)/lib -Istubs -DSTUB_TRACK_ALLOC -pthread \
		-o $@ $(FORMATS_SRC) $(FW_OBJ)

decoders: $(BUILD)/test_fwdecode $(BUILD)/test_fwpreset
	$(BUILD)/test_fwdecode
	$(BUILD)/test_fwpreset

$(BUILD)/test_fwpreset: $(FWPRESET_SRC) ../helpers/rg_preset.h $(FW)/.fetched | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -I$(FW) -I$(FW)/lib/drivers -Istubs -DFW_DIR='"$(FW)"' -o $@ \
		$(FWPRESET_SRC)

$(SUBGHZ_SRC): $(FW)/.fetched ;

captures: $(BUILD)/test_fwanalyze
	$(BUILD)/test_fwanalyze

$(BUILD)/test_fwanalyze: $(FWANALYZE_SRC) ../helpers/rg_analyzer.h ../helpers/rg_raw.h \
		$(FW)/.fetched | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ $(FWANALYZE_SRC)

# lib/subghz finds its own headers through its folder (as the firmware's
# build adds it) and mlib through lib/mlib. STUB_FURI_HEAP: malloc hands out
# zeroed blocks, as the firmware's does (stubs/furi.h).
DEC_INC := -I$(FW) -I$(FW)/lib -I$(FW)/lib/subghz -I$(FW)/lib/mlib -Istubs -DSTUB_FURI_HEAP

# The firmware's code (here with its FlipperFormat code again, built the same
# way) is compiled as it is. It shifts bytes into the sign bit of an int
# (alutech_at_4n.c: p[0] << 24), which C leaves undefined and GCC, the
# firmware's compiler, defines; UBSan's shift-base check is off for these
# objects only. No warning needed turning off.
$(BUILD)/fwdec/%.o: $(FW)/%.c $(DEC_STUB_HDRS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SAN) -fno-sanitize=shift-base -D_GNU_SOURCE $(DEC_INC) -c -o $@ $<

$(BUILD)/test_fwdecode: $(FWDECODE_SRC) $(DEC_STUB_HDRS) ../helpers/rg_decode.h ../helpers/rg_raw.h \
		../helpers/radiogeddon_decode_text.h ../helpers/radiogeddon_bands.h \
		$(DEC_FW_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -D_GNU_SOURCE $(DEC_INC) -pthread \
		-o $@ $(FWDECODE_SRC) $(DEC_FW_OBJ) -lm

$(BUILD)/replay_fuzz_raw: fuzz/replay.c $(FUZZ_RAW_SRC) ../helpers/rg_raw.h \
		../helpers/rg_analyzer.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ fuzz/replay.c $(FUZZ_RAW_SRC)

$(BUILD)/replay_fuzz_db: fuzz/replay.c $(FUZZ_DB_SRC) ../helpers/rg_db.h \
		../helpers/radiogeddon_dsp.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ fuzz/replay.c $(FUZZ_DB_SRC)

$(BUILD)/replay_fuzz_samples: fuzz/replay.c $(FUZZ_SAMPLES_SRC) ../helpers/rg_analyzer.h \
		../helpers/rg_timeline.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -o $@ fuzz/replay.c $(FUZZ_SAMPLES_SRC)

# libFuzzer builds. New inputs go to build/corpus/<target>; the committed
# corpus is only read. A failing input is saved as build/<target>-crash-*.
$(BUILD)/fuzz_raw: $(FUZZ_RAW_SRC) | $(BUILD)
	$(FUZZ_CC) $(CFLAGS) -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
		-o $@ $(FUZZ_RAW_SRC)

$(BUILD)/fuzz_db: $(FUZZ_DB_SRC) | $(BUILD)
	$(FUZZ_CC) $(CFLAGS) -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
		-o $@ $(FUZZ_DB_SRC)

$(BUILD)/fuzz_samples: $(FUZZ_SAMPLES_SRC) | $(BUILD)
	$(FUZZ_CC) $(CFLAGS) -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
		-o $@ $(FUZZ_SAMPLES_SRC)

fuzz: $(addprefix $(BUILD)/,$(FUZZ_TARGETS))
	for t in $(FUZZ_TARGETS); do \
		mkdir -p $(BUILD)/corpus/$$t && \
		$(BUILD)/$$t -max_total_time=$(FUZZ_TIME) -max_len=16384 -print_final_stats=1 \
			-artifact_prefix=$(BUILD)/$$t- $(BUILD)/corpus/$$t fuzz/corpus/$$t || exit 1; \
	done

# Optional: the ring under ThreadSanitizer (not part of check; needs TSan).
tsan: | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=thread -pthread -o $(BUILD)/test_ring_tsan $(RING_SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -Istubs -DSTUB_TRACK_ALLOC -pthread \
		-o $(BUILD)/test_recorder_tsan $(RECORDER_SRC)
	$(BUILD)/test_ring_tsan
	$(BUILD)/test_recorder_tsan

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) test_dsp test_analyzer test_scan test_hop test_raw test_timeline test_ring \
		test_rawfmt test_recorder test_db test_memstat test_dbload test_decode test_freq test_preset test_fwpreset test_fwanalyze
