# blip25c decoder: static library, ambe2wav, tests.   make; make test
CC ?= cc
CFLAGS ?= -O2
B25_CFLAGS = $(CFLAGS) -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -Iinclude -Isrc
SRC = src/b25_dsp_a.c src/b25_dsp_b.c src/b25_decode_c.c src/b25_decode_d.c src/b25_tables.c src/b25_golay.c src/blip25.c src/mbelib_compat.c
OBJ = $(SRC:.c=.o)

all: libblip25.a ambe2wav tests/run_tests tests/bench

%.o: %.c
	$(CC) $(B25_CFLAGS) -c $< -o $@

libblip25.a: $(OBJ)
	ar rcs $@ $^

ambe2wav: examples/ambe2wav.c libblip25.a
	$(CC) $(B25_CFLAGS) $< libblip25.a -o $@

tests/run_tests: tests/run_tests.c tests/expected.h libblip25.a
	$(CC) $(B25_CFLAGS) -Itests $< libblip25.a -o $@

tests/bench: tests/bench.c libblip25.a
	$(CC) $(B25_CFLAGS) $< libblip25.a -o $@

test: tests/run_tests tests/bench
	./tests/run_tests tests/vectors
	./tests/bench tests/vectors/speech.bin

clean:
	rm -f $(OBJ) libblip25.a ambe2wav tests/run_tests tests/bench

.PHONY: all test clean
