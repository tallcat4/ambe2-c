# ambe-enc: static library + tests.   make; make test
CC ?= cc
CFLAGS ?= -O2
ENC_CFLAGS = $(CFLAGS) -std=c99 -Wall -Wextra -Iinclude -Isrc
SRC = src/encoder.c src/analysis.c src/quant.c src/fft.c src/enc_tables.c
OBJ = $(SRC:.c=.o)

all: libambeenc.a wav2ambe tests/encode_raw tests/bench

wav2ambe: examples/wav2ambe.c libambeenc.a
	$(CC) $(ENC_CFLAGS) $< libambeenc.a -lm -o $@

%.o: %.c
	$(CC) $(ENC_CFLAGS) -c $< -o $@

libambeenc.a: $(OBJ)
	ar rcs $@ $^

tests/encode_raw: tests/encode_raw.c libambeenc.a
	$(CC) $(ENC_CFLAGS) $< libambeenc.a -lm -o $@

tests/bench: tests/bench.c libambeenc.a
	$(CC) $(ENC_CFLAGS) $< libambeenc.a -lm -o $@

# the expected frames come from the numpy reference (lite/lite_encoder.py) at the time the tables were generated
test: tests/encode_raw tests/bench
	@for v in ja en noisy; do \
	  ./tests/encode_raw tests/vectors/$$v.pcm tests/$$v.out.ambe; \
	  if cmp -s tests/$$v.out.ambe tests/vectors/$$v.expected.ambe; then echo "$$v: identical to the reference"; \
	  else echo "$$v: DIFFERENT from the reference"; fi; \
	  ./tests/encode_raw tests/vectors/$$v.pcm tests/$$v.bg.out.ambe --background; \
	  if cmp -s tests/$$v.bg.out.ambe tests/vectors/$$v.background.expected.ambe; then echo "$$v --background: identical to the reference"; \
	  else echo "$$v --background: DIFFERENT from the reference"; fi; \
	done
	@./tests/bench tests/vectors/ja.pcm
	@./tests/bench tests/vectors/ja.pcm --background

clean:
	rm -f $(OBJ) libambeenc.a wav2ambe tests/encode_raw tests/bench tests/*.out.ambe

.PHONY: all test clean
