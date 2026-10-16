#!/bin/bash
# Build the ESP-IDF test app for one target / encoder variant and run it in Espressif QEMU with instruction counting.
#   tests/mcu/run_esp.sh esp32|esp32s3|esp32c3 fixed|float|double [profile]
# With ESPPORT=/dev/... set, flashes a real board instead and reads its report from the serial port.
# AMBE_ENC_RAM=1 places the encoder's code and tables in internal RAM (see examples/esp32/main/ambe_enc_ram.lf).
# Needs ESP-IDF in the environment (. export.sh) and its QEMU (idf_tools.py install qemu-xtensa qemu-riscv32).
set -e
target=$1 variant=$2 prof=$3
app=$(cd "$(dirname "$0")/../../examples/esp32" && pwd)
b=build_${target}_${variant}${prof:+_$prof}${AMBE_ENC_RAM:+_ram}
cd "$app"
idf.py -B "$b" -DIDF_TARGET="$target" -DSDKCONFIG="$b/sdkconfig" -DAMBE_ENC_VARIANT="$variant" -DAMBE_ENC_PROFILE=${prof:+1} -DAMBE_ENC_RAM=${AMBE_ENC_RAM} build >"$b.log" 2>&1 ||
    { tail -30 "$b.log"; exit 1; }
if [ -n "$ESPPORT" ]; then
    idf.py -B "$b" -DIDF_TARGET="$target" -DSDKCONFIG="$b/sdkconfig" -p "$ESPPORT" flash >"$b.flash.log" 2>&1 ||
        { tail -20 "$b.flash.log"; exit 1; }
    python - "$ESPPORT" "$variant" <<'PY'
import sys, time, serial
s = serial.Serial(sys.argv[1], 115200, timeout=1)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # reset into the application
t0 = time.time()
while time.time() - t0 < 300:
    line = s.readline().decode(errors="replace").rstrip()
    if any(k in line for k in ("calibration", "encoder state", "frames identical", "too small", "ticks/frame", "abort", "Guru", "decoder")):
        print(line, flush=True)
    if "AMBE_DEC_TEST_DONE" in line or ("AMBE_ENC_TEST_DONE" in line and "fixed" not in sys.argv[2]):
        break
PY
    exit 0
fi
cd "$b"
esptool.py --chip "$target" merge_bin --fill-flash-size 4MB -o flash.bin @flash_args >/dev/null
case $target in
esp32c3) q="qemu-system-riscv32 -machine esp32c3" ;;
*) q="qemu-system-xtensa -machine $target -m 4M" ;;
esac
# -icount shift=0: the virtual clock advances 1 ns per instruction; the test prints its calibration
$q -nographic -icount shift=0,align=off -drive file=flash.bin,if=mtd,format=raw -nic none >qemu.log 2>&1 &
pid=$!
until grep -q -e AMBE_ENC_TEST_DONE -e "too small" -e "abort()" -e "Guru Meditation" qemu.log 2>/dev/null; do
    kill -0 $pid 2>/dev/null || break
    sleep 1
done
kill $pid 2>/dev/null || true
grep -e calibration -e "encoder state" -e "frames identical" -e "too small" -e "abort()" -e "Guru Meditation" -e "ticks/frame" qemu.log
