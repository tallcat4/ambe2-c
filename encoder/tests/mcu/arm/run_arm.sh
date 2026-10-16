#!/bin/bash
# Build the encoder test for one Cortex-M core / variant and run it in QEMU (MPS2 board, instruction counting).
#   tests/mcu/arm/run_arm.sh m0|m3|m4|m7 fixed|float|double
# Needs arm-none-eabi-gcc (with newlib) and qemu-system-arm on PATH.
set -e
cpu=$1 variant=$2 extra=$3   # extra: e.g. -DFX_PROFILE
here=$(cd "$(dirname "$0")" && pwd)
enc=$(cd "$here/../../.." && pwd)
case $cpu in
m0) flags="-mcpu=cortex-m0 -mthumb -mfloat-abi=soft" machine=mps2-an385 ;;   # ARMv6-M code on the M3 board
m3) flags="-mcpu=cortex-m3 -mthumb -mfloat-abi=soft" machine=mps2-an385 ;;
m4) flags="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard" machine=mps2-an386 ;;
m7) flags="-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard" machine=mps2-an500 ;;
*) echo "unknown cpu $cpu"; exit 2 ;;
esac
case $variant in
fixed) src="fx_encoder.c fx_analysis.c fx_quant.c fx_dsp.c fx_tables.c" def=-DAMBE_ENC_FIXED exp=fixed. ;;
float) src="encoder.c analysis.c quant.c fft.c enc_tables.c" def=-DAMBE_ENC_FLOAT exp= ;;
double) src="encoder.c analysis.c quant.c fft.c enc_tables.c" def= exp= ;;
esac
out=$here/build/${cpu}_${variant}
mkdir -p "$out"
(cd "$enc/tests/vectors" && { xxd -i noisy.pcm; xxd -i -n noisy_expect "noisy.${exp}expected.ambe";
  xxd -i -n noisy_expect_bg "noisy.${exp}background.expected.ambe"; }) |
  sed 's/^unsigned char/const unsigned char/; s/^unsigned int/const unsigned int/' >"$out/vectors.c"
arm-none-eabi-gcc $flags -O2 -std=c99 $def $extra -ffunction-sections -fdata-sections -I"$enc/include" -I"$enc/src" -I"$enc/tests/mcu" \
    "$here/startup.c" "$enc/tests/mcu/mcu_test.c" "$out/vectors.c" $(for s in $src; do echo "$enc/src/$s"; done) \
    -T "$here/mps2.ld" --specs=rdimon.specs -nostartfiles -Wl,--gc-sections,--no-warn-rwx-segments -lm -o "$out/test.elf"
arm-none-eabi-size "$out/test.elf" | tail -1 | awk '{printf "image: text %d B, data %d B, bss %d B\n", $1, $2, $3}'
qemu-system-arm -machine $machine -nographic -semihosting-config enable=on,target=native -icount shift=0,align=off \
    -kernel "$out/test.elf" 2>&1 | grep -e calibration -e "encoder state" -e "frames identical" -e "too small" -e "ticks/frame"
