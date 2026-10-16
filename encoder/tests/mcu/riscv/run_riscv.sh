#!/bin/bash
# Build the encoder test for RV32 (no FPU: rv32imc, or rv32imfc for float) and run it in QEMU's "virt" board, which
# counts retired instructions exactly (minstret).   tests/mcu/riscv/run_riscv.sh rv32imc|rv32imac|rv32imfc fixed|float
# Needs a riscv32 ELF gcc (e.g. ESP-IDF's riscv32-esp-elf-gcc; no C library is linked) and
# qemu-system-riscv32.
set -e
arch=$1 variant=$2 extra=$3
here=$(cd "$(dirname "$0")" && pwd)
enc=$(cd "$here/../../.." && pwd)
CC=${CC:-riscv32-esp-elf-gcc}
case $arch in *f*) abi=ilp32f ;; *) abi=ilp32 ;; esac
case $variant in
fixed) src="fx_encoder.c fx_analysis.c fx_quant.c fx_dsp.c fx_tables.c" def=-DAMBE_ENC_FIXED exp=fixed. ;;
float) src="encoder.c analysis.c quant.c fft.c enc_tables.c" def=-DAMBE_ENC_FLOAT exp= ;;
double) src="encoder.c analysis.c quant.c fft.c enc_tables.c" def= exp= ;;
esac
out=$here/build/${arch}_${variant}
mkdir -p "$out"
(cd "$enc/tests/vectors" && { xxd -i noisy.pcm; xxd -i -n noisy_expect "noisy.${exp}expected.ambe";
  xxd -i -n noisy_expect_bg "noisy.${exp}background.expected.ambe"; }) |
  sed 's/^unsigned char/const unsigned char/; s/^unsigned int/const unsigned int/' >"$out/vectors.c"
$CC -march=${arch}_zicsr -mabi=$abi -O2 -std=c99 $def $extra -ffunction-sections -fdata-sections -I"$enc/include" -I"$enc/src" \
    -I"$enc/tests/mcu" "$here/start.c" "$enc/tests/mcu/mcu_test.c" "$out/vectors.c" \
    $(for s in $src; do echo "$enc/src/$s"; done) -T "$here/virt.ld" -nostartfiles -nostdlib -Wl,--gc-sections,--no-warn-rwx-segments -lgcc -o "$out/test.elf"
qemu-system-riscv32 -machine virt -bios none -nographic -icount shift=0,align=off -kernel "$out/test.elf" 2>&1 |
    grep -e calibration -e "encoder state" -e "frames identical" -e "too small" -e "ticks/frame"
