# AMBE_ENC_VARIANT: fixed (integer only, default), float or double; AMBE_ENC_RAM=1: encoder in internal RAM
set(ENC ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DEC ${ENC}/../decoder)
set(DEC_SRCS b25_dsp_a.c b25_dsp_b.c b25_decode_c.c b25_decode_d.c b25_tables.c b25_golay.c blip25.c)
list(TRANSFORM DEC_SRCS PREPEND ${DEC}/src/)
if(NOT DEFINED AMBE_ENC_VARIANT)
    set(AMBE_ENC_VARIANT fixed)
endif()
if(AMBE_ENC_VARIANT STREQUAL "fixed")
    set(ENC_SRCS fx_encoder.c fx_analysis.c fx_quant.c fx_dsp.c fx_tables.c)
    set(ENC_DEF AMBE_ENC_FIXED)
    set(EXP fixed.)
else()
    set(ENC_SRCS encoder.c analysis.c quant.c fft.c enc_tables.c)
    set(EXP "")
    if(AMBE_ENC_VARIANT STREQUAL "float")
        set(ENC_DEF AMBE_ENC_FLOAT)
    else()
        set(ENC_DEF AMBE_ENC_DOUBLE)
    endif()
endif()
list(TRANSFORM ENC_SRCS PREPEND ${ENC}/src/)
if(AMBE_ENC_RAM)
    set(ENC_LF ambe_enc_ram.lf)   # code and tables in internal RAM
endif()
idf_component_register(
    SRCS "main.c" "${ENC}/tests/mcu/mcu_test.c" ${ENC_SRCS} "${ENC}/tests/mcu/dec_test.c" ${DEC_SRCS}
    INCLUDE_DIRS "${ENC}/include" "${ENC}/src" "${ENC}/tests/mcu" "${DEC}/include"
    LDFRAGMENTS ${ENC_LF}
    EMBED_FILES "${ENC}/tests/vectors/noisy.pcm" "${ENC}/tests/vectors/noisy.${EXP}expected.ambe"
                "${ENC}/tests/vectors/noisy.${EXP}background.expected.ambe" "${DEC}/tests/vectors/chip3600.bin")
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${ENC_DEF} EXP_${AMBE_ENC_VARIANT} MCU_TEST_MALLOC)
if(AMBE_ENC_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FX_PROFILE)
endif()
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
