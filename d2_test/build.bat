@echo off
set "PYTHONUTF8=1"
call C:\Users\ioten\zephyrproject\.venv\Scripts\activate.bat
cd /d C:\Users\ioten\zephyrproject
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s C:\Users\ioten\tron_zephyr\d2_test -d C:\Users\ioten\zephyrproject\build_d2_test -p auto
if %ERRORLEVEL% equ 0 (
    if not exist C:\Users\ioten\tron_zephyr\d2_test\build\zephyr mkdir C:\Users\ioten\tron_zephyr\d2_test\build\zephyr
    copy /y C:\Users\ioten\zephyrproject\build_d2_test\zephyr\zephyr.elf C:\Users\ioten\tron_zephyr\d2_test\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_d2_test\zephyr\zephyr.hex C:\Users\ioten\tron_zephyr\d2_test\build\zephyr\
    copy /y C:\Users\ioten\zephyrproject\build_d2_test\zephyr\zephyr.bin C:\Users\ioten\tron_zephyr\d2_test\build\zephyr\
    echo.
    echo ========================================
    echo Build SUCCESS!
    echo Binary: build\zephyr\zephyr.elf
    echo ========================================
)
