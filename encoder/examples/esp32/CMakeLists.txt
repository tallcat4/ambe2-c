# ESP-IDF test app (tests/mcu/mcu_test.c): encodes the noisy test vector on the target or in Espressif QEMU, checks the
# frames and prints cycles per frame.   idf.py -DAMBE_ENC_VARIANT=fixed|float|double set-target esp32 build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ambe_enc_test)
