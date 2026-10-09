# Host-side unit tests for RadioGeddon's firmware-independent DSP helpers.
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -I..

test_dsp: test_dsp.c ../helpers/radiogeddon_dsp.c ../helpers/radiogeddon_dsp.h
	$(CC) $(CFLAGS) -o test_dsp test_dsp.c ../helpers/radiogeddon_dsp.c

.PHONY: check clean
check: test_dsp
	./test_dsp

clean:
	rm -f test_dsp
