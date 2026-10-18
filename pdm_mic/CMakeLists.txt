# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pdm_mic)

# D/AVE 2D sources
file(GLOB DAVE2D_SRCS
    "src/dave2d/src/*.c"
)

# DRW low-level driver sources
set(DRW_SRCS
    src/drw/r_drw_base.c
    src/drw/r_drw_memory.c
    src/drw/r_drw_irq.c
)

# PDM & DMAC driver sources
set(PDM_SRCS
    src/pdm/r_pdm.c
    src/pdm/r_dmac.c
    src/pdm/pdm_audio.c
)

target_sources(app PRIVATE
    src/main.cpp
    ${DAVE2D_SRCS}
    ${DRW_SRCS}
    ${PDM_SRCS}
)

target_include_directories(app PRIVATE
    src
    src/drw
    src/dave2d/inc
    src/dave2d/src
    src/pdm
)

# Suppress minor third-party compiler warnings
target_compile_options(app PRIVATE
    -Wno-unused-parameter
    -Wno-missing-declarations
)
