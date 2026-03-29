# Replay tool that links against Mesa for NIR deserialization and
# includes mypipe's vertex pipeline code directly.
#
#   make
#   ./replay /tmp/q3_trace.bin              # first 20 draws
#   ./replay /tmp/q3_trace.bin 5            # first 5 draws
#   ./replay /tmp/q3_trace.bin 1802 314     # only draw #314

MESA      = ../../../../..
BUILD     = $(MESA)/build
MYPIPE    = ..

CC        = gcc
CFLAGS    = -Wall -g -O0 -Wno-unused-function
CFLAGS   += -I$(MESA)/include \
            -I$(MESA)/src \
            -I$(MESA)/src/gallium/include \
            -I$(MESA)/src/gallium/auxiliary \
            -I$(MESA)/src/gallium/drivers/mypipe \
            -I$(BUILD)/src \
            -I$(BUILD)/src/compiler \
            -I$(BUILD)/src/compiler/nir

# Link against the Mesa build's static libraries for NIR + util_format
LIBS      = $(BUILD)/src/compiler/nir/libnir.a \
            $(BUILD)/src/compiler/libcompiler.a \
            $(BUILD)/src/util/libmesa_util.a \
            $(BUILD)/src/util/format/libmesa_format.a \
            $(BUILD)/src/gallium/drivers/mypipe/libmypipe.a \
            -lm -lpthread -ldl -lz -lzstd

replay: replay.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f replay

.PHONY: clean
