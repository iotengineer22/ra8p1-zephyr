# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(d2_test)

# Dave2D sources
file(GLOB DAVE2D_SRCS
    "src/dave2d/src/*.c"
)

# DRW low-level driver sources
set(DRW_SRCS
    src/drw/r_drw_base.c
    src/drw/r_drw_memory.c
    src/drw/r_drw_irq.c
)

target_sources(app PRIVATE
    src/main.c
    ${DAVE2D_SRCS}
    ${DRW_SRCS}
)

target_include_directories(app PRIVATE
    src
    src/drw
    src/dave2d/inc
    src/dave2d/src
)

# Compiler definitions for Dave2D
target_compile_definitions(app PRIVATE
    DRW_CFG_USE_DLIST_INDIRECT=1
    DRW_CFG_CUSTOM_MALLOC=0
)
