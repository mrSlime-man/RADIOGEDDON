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
RECORDER_SRC := test_recorder.c stubs/furi_stub.c ../helpers/radiogeddon_recorder.c \
		../helpers/rg_ring.c ../helpers/rg_rawfmt.c ../helpers/rg_raw.c

.PHONY: check clean tsan
check: $(BUILD)/test_dsp $(BUILD)/test_analyzer $(BUILD)/test_scan $(BUILD)/test_hop $(BUILD)/test_raw \
		$(BUILD)/test_timeline $(BUILD)/test_ring $(BUILD)/test_rawfmt $(BUILD)/test_recorder
	$(BUILD)/test_dsp
	$(BUILD)/test_analyzer
	$(BUILD)/test_scan
	$(BUILD)/test_hop
	$(BUILD)/test_raw
	$(BUILD)/test_timeline
	$(BUILD)/test_ring
	$(BUILD)/test_rawfmt
	$(BUILD)/test_recorder

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

# The recorder is device code: it builds against small Furi/Storage stand-ins.
$(BUILD)/test_recorder: $(RECORDER_SRC) ../helpers/radiogeddon_recorder.h stubs/furi.h \
		stubs/storage/storage.h | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) -Istubs -pthread -o $@ $(RECORDER_SRC)

# Optional: the ring under ThreadSanitizer (not part of check; needs TSan).
tsan: | $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=thread -pthread -o $(BUILD)/test_ring_tsan $(RING_SRC)
	$(CC) $(CFLAGS) -fsanitize=thread -Istubs -pthread -o $(BUILD)/test_recorder_tsan $(RECORDER_SRC)
	$(BUILD)/test_ring_tsan
	$(BUILD)/test_recorder_tsan

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) test_dsp test_analyzer test_scan test_hop test_raw test_timeline test_ring \
		test_rawfmt test_recorder
