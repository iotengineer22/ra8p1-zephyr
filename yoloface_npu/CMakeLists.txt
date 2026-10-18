# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(yoloface_npu)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/camera
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mipi
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drw
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/font
    ${CMAKE_CURRENT_SOURCE_DIR}/src/npu
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/ruhmi
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/face_detection
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai/common
)

# Collect D/AVE 2D sources
file(GLOB DAVE2D_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/src/dave2d/src/*.c")

target_sources(app PRIVATE
    src/main.cpp

    # Camera Subsystem
    src/camera/camera_layer.c
    src/camera/camera_utils.c

    # MIPI-CSI2 & VIN Subsystem
    src/mipi/mipi_vin_config.c
    src/mipi/r_mipi_csi.c
    src/mipi/r_mipi_phy.c
    src/mipi/r_vin.c

    # D/AVE 2D (DRW) Graphics Subsystem
    src/drw/r_drw_base.c
    src/drw/r_drw_irq.c
    src/drw/r_drw_memory.c
    ${DAVE2D_SRCS}

    # Font
    src/font/bg_font_18_full.c

    # Arm Ethos-U55 NPU Driver
    src/npu/rm_ethosu.c
    src/npu/ethosu_driver.c
    src/npu/ethosu_device_u55_u65.c
    src/npu/ethosu_pmu.c
    src/npu/ethosu_irq.c

    # AI Model & Inference Pipeline
    src/ai/time_counter.c
    src/ai/ai_inference.cpp
    src/ai/ruhmi/model.c
    src/ai/ruhmi/model_io_data.c
    src/ai/ruhmi/sub_0000_command_stream.c
    src/ai/ruhmi/sub_0000_invoke.c
    src/ai/ruhmi/sub_0000_io_data.c
    src/ai/ruhmi/sub_0000_model_data.c
    src/ai/ruhmi/sub_0000_tensors.c
    src/ai/face_detection/MainLoop_obj.cc
    src/ai/face_detection/DetectorPostProcessing.cc
    src/ai/common/ImageUtils.cc
    src/ai/common/PlatformMath.cc
)

# Suppress minor third-party compiler warnings
target_compile_options(app PRIVATE
    -Wno-unused-parameter
    -Wno-missing-declarations
    -Wno-format
    -Wno-stringop-overflow
    -Wno-conversion
)
