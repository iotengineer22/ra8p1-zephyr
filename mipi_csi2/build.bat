@echo off
set "PYTHONUTF8=1"
call C:\Users\ioten\zephyrproject\.venv\Scripts\activate.bat
cd /d C:\Users\ioten\zephyrproject
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s C:\Users\ioten\tron_zephyr\mipi_csi2 -d C:\Users\ioten\zephyrproject\build_mipi_csi2 -p auto
if %ERRORLEVEL% equ 0 (
    if not exist C:\Users\ioten\tron_zephyr\mipi_csi2\build\zephyr mkdir C:\Users\ioten\tron_zephyr\mipi_csi2\build\zephyr
    copy /y C:\Users\ioten\zephyrproject\build_mipi_csi2\zephyr\zephyr.elf C:\Users\ioten\tron_zephyr\mipi_csi2\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_mipi_csi2\zephyr\zephyr.hex C:\Users\ioten\tron_zephyr\mipi_csi2\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_mipi_csi2\zephyr\zephyr.bin C:\Users\ioten\tron_zephyr\mipi_csi2\build\zephyr\
    echo.
    echo ========================================
    echo Build SUCCESS!
    echo Binary: build\zephyr\zephyr.elf
    echo ========================================
)
