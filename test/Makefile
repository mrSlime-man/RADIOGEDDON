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

.PHONY: check clean
check: $(BUILD)/test_dsp $(BUILD)/test_analyzer $(BUILD)/test_scan $(BUILD)/test_hop $(BUILD)/test_raw
	$(BUILD)/test_dsp
	$(BUILD)/test_analyzer
	$(BUILD)/test_scan
	$(BUILD)/test_hop
	$(BUILD)/test_raw

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

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) test_dsp test_analyzer test_scan test_hop test_raw
