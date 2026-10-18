@echo off
set "PYTHONUTF8=1"
call C:\Users\ioten\zephyrproject\.venv\Scripts\activate.bat
cd /d C:\Users\ioten\zephyrproject
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s C:\Users\ioten\tron_zephyr\yoloface_cpu -d C:\Users\ioten\zephyrproject\build_yoloface_cpu -p auto
if %ERRORLEVEL% equ 0 (
    if not exist C:\Users\ioten\tron_zephyr\yoloface_cpu\build\zephyr mkdir C:\Users\ioten\tron_zephyr\yoloface_cpu\build\zephyr
    copy /y C:\Users\ioten\zephyrproject\build_yoloface_cpu\zephyr\zephyr.elf C:\Users\ioten\tron_zephyr\yoloface_cpu\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_yoloface_cpu\zephyr\zephyr.hex C:\Users\ioten\tron_zephyr\yoloface_cpu\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_yoloface_cpu\zephyr\zephyr.bin C:\Users\ioten\tron_zephyr\yoloface_cpu\build\zephyr\
    echo.
    echo ========================================
    echo Build SUCCESS!
    echo Binary: build\zephyr\zephyr.elf
    echo ========================================
)
