#!/bin/bash
# Build the encoder test for the Arduino Portenta H7 (M7 core), upload it and print its report.
#   tests/mcu/portenta/run_portenta.sh fixed|float|double [port] [extra cflags, e.g. -DFX_PROFILE]
# Needs arduino-cli with the arduino:mbed_portenta core, and python3 with pyserial.
set -e
variant=$1 port=${2:-$(arduino-cli board list | awk '/mbed_portenta/ {print $1; exit}')} extra=$3
here=$(cd "$(dirname "$0")" && pwd)
enc=$(cd "$here/../../.." && pwd)
dec=$(cd "$enc/../decoder" && pwd)
case $variant in
fixed) src="fx_encoder fx_analysis fx_quant fx_dsp fx_tables" def=-DAMBE_ENC_FIXED exp=fixed. ;;
float) src="encoder analysis quant fft enc_tables" def=-DAMBE_ENC_FLOAT exp= ;;
double) src="encoder analysis quant fft enc_tables" def= exp= ;;
esac
sk=$here/build/$variant/ambe_enc_test
rm -rf "$sk" && mkdir -p "$sk"
cp "$here/ambe_enc_test.ino" "$here/glue.cpp" "$sk/"
for s in $src b25_dsp_a b25_dsp_b b25_decode_c b25_decode_d b25_tables b25_golay blip25; do
    echo "#include \"$s.c\"" >"$sk/w_$s.c"   # one translation unit per source (encoder, decoder)
done
printf '#define printf mcu_printf\n#include "dec_test.c"\n' >"$sk/w_dec_test.c"
printf '#define printf mcu_printf\n#include "mcu_test.c"\n' >"$sk/w_mcu_test.c"
(cd "$enc/tests/vectors" && { xxd -i noisy.pcm; xxd -i -n noisy_expect "noisy.${exp}expected.ambe";
  xxd -i -n noisy_expect_bg "noisy.${exp}background.expected.ambe"; xxd -i -n dec_r34 noisy.fixed.expected.ambe;
  xxd -i -n dec_r33 "$dec/tests/vectors/chip3600.bin"; }) |
  sed 's/^unsigned char/const unsigned char/; s/^unsigned int/const unsigned int/' >"$sk/vectors.c"
flags="-O2 $def $extra -I$enc/include -I$enc/src -I$enc/tests/mcu -I$dec/include -I$dec/src"
arduino-cli compile --fqbn arduino:mbed_portenta:envie_m7 --build-property "compiler.c.extra_flags=$flags" \
    --build-property "compiler.cpp.extra_flags=$flags" "$sk" 2>&1 | grep -E "error|Sketch uses|Global variables" || true
arduino-cli upload --fqbn arduino:mbed_portenta:envie_m7 -p "$port" "$sk" >/dev/null 2>&1 || arduino-cli upload --fqbn arduino:mbed_portenta:envie_m7 -p "$port" "$sk"
for i in $(seq 30); do   # the board re-enumerates after the upload
    sleep 1
    port=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
    [ -n "$port" ] && break
done
python3 - "$port" <<'PY'
import sys, time, serial
s = serial.Serial(sys.argv[1], 115200, timeout=1)
t0 = time.time()
while time.time() - t0 < 600:
    line = s.readline().decode(errors="replace").rstrip()
    if line:
        print(line, flush=True)
    if "AMBE_DEC_TEST_DONE" in line or "too small" in line:
        break
PY
