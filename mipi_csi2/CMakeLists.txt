# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mipi_csi2)

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

# MIPI & VIN driver sources
set(MIPI_SRCS
    src/mipi/r_mipi_csi.c
    src/mipi/r_vin.c
    src/mipi/r_mipi_phy.c
    src/mipi/mipi_vin_config.c
)

# Camera layer sources
set(CAM_SRCS
    src/camera/camera_layer.c
    src/camera/camera_utils.c
)

target_sources(app PRIVATE
    src/main.c
    ${DAVE2D_SRCS}
    ${DRW_SRCS}
    ${MIPI_SRCS}
    ${CAM_SRCS}
)

target_include_directories(app PRIVATE
    src
    src/mipi
    src/camera
    src/drw
    src/dave2d/inc
    src/dave2d/src
)
