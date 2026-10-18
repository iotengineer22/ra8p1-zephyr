# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pdm_cpu)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drw
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pdm
    ${CMAKE_CURRENT_SOURCE_DIR}/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model-parameters
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tflite-model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/edge-impulse-sdk
)

# Collect D/AVE 2D sources
file(GLOB DAVE2D_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/src/*.c")

target_sources(app PRIVATE
    src/main.cpp

    # D/AVE 2D (DRW) Graphics Subsystem
    src/drw/r_drw_base.c
    src/drw/r_drw_irq.c
    src/drw/r_drw_memory.c
    ${DAVE2D_SRCS}

    # Font
    src/font/bg_font_18_full.c

    # PDM Audio Subsystem
    src/pdm/r_pdm.c
    src/pdm/r_dmac.c
    src/pdm/pdm_audio.c

    # Edge Impulse EON Compiled Model (CPU / CMSIS-NN)
    src/tflite-model/compute_sub_0000.c
    src/tflite-model/kernel_library_int.c
    src/tflite-model/kernel_library_utils.c
    src/tflite-model/model_io_data.c
    src/tflite-model/tflite_learn_943529_3_compiled.cpp

    # Edge Impulse SDK
    src/edge-impulse-sdk/dsp/kissfft/kiss_fft.cpp
    src/edge-impulse-sdk/dsp/kissfft/kiss_fftr.cpp
    src/edge-impulse-sdk/dsp/dct/fast-dct-fft.cpp
    src/edge-impulse-sdk/dsp/memory.cpp
    src/edge-impulse-sdk/classifier/ei_run_classifier_c.cpp
    src/edge-impulse-sdk/porting/zephyr/ei_classifier_porting.cpp
    src/edge-impulse-sdk/porting/zephyr/debug_log.cpp
)

target_compile_definitions(app PRIVATE
    TF_LITE_STATIC_MEMORY
    EI_PORTING_ZEPHYR=1
)

target_compile_options(app PRIVATE
    -Wno-unused-parameter
    -Wno-missing-declarations
    -Wno-format
    -Wno-stringop-overflow
    -Wno-conversion
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>
)
