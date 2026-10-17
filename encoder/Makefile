# ambe-enc: static library + tests.   make; make test
#   make FIXED=1 ...   integer-only version (src/fx_*.c): no floating point at all, for MCUs without an FPU
CC ?= cc
CFLAGS ?= -O2
ENC_CFLAGS = $(CFLAGS) -std=c99 -Wall -Wextra -Iinclude -Isrc
ifeq ($(FIXED),1)
SRC = src/fx_encoder.c src/fx_analysis.c src/fx_quant.c src/fx_dsp.c src/fx_tables.c src/fec.c
ENC_CFLAGS += -DAMBE_ENC_FIXED
EXPECT = fixed.
else
SRC = src/encoder.c src/analysis.c src/quant.c src/fft.c src/enc_tables.c src/fec.c
EXPECT =
endif
OBJ = $(SRC:.c=.o)

all: libambeenc.a wav2ambe tests/encode_raw tests/bench

DEC = ../decoder

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

# the FEC test checks the rate-33 frames against the chip and against the decoder's FEC (../decoder)
$(DEC)/libblip25.a: FORCE
	$(MAKE) -C $(DEC) libblip25.a

tests/fec_test: tests/fec_test.c libambeenc.a $(DEC)/libblip25.a
	$(CC) $(ENC_CFLAGS) -I$(DEC)/include $< libambeenc.a $(DEC)/libblip25.a -lm -o $@

# the expected frames come from the numpy reference (lite/lite_encoder.py) at the time the tables were generated
test: tests/encode_raw tests/bench tests/fec_test
	@for v in en noisy; do \
	  ./tests/encode_raw tests/vectors/$$v.pcm tests/$$v.out.ambe; \
	  if cmp -s tests/$$v.out.ambe tests/vectors/$$v.$(EXPECT)expected.ambe; then echo "$$v: identical to the reference"; \
	  else echo "$$v: DIFFERENT from the reference"; fi; \
	  ./tests/encode_raw tests/vectors/$$v.pcm tests/$$v.bg.out.ambe --background; \
	  if cmp -s tests/$$v.bg.out.ambe tests/vectors/$$v.$(EXPECT)background.expected.ambe; then echo "$$v --background: identical to the reference"; \
	  else echo "$$v --background: DIFFERENT from the reference"; fi; \
	done
	@./tests/fec_test $(DEC)/tests/vectors/chip3600.bin
	@./tests/bench tests/vectors/en.pcm
	@./tests/bench tests/vectors/en.pcm --background

clean:
	rm -f src/*.o libambeenc.a wav2ambe tests/encode_raw tests/bench tests/fec_test tests/*.out.ambe

.PHONY: all test clean FORCE
