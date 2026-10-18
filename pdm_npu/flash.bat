@echo off
set "PATH=C:\Program Files\SEGGER\JLink_V942;%PATH%"
set "PYTHONUTF8=1"
call C:\Users\ioten\zephyrproject\.venv\Scripts\activate.bat
cd /d C:\Users\ioten\zephyrproject
west flash -d build_pdm_npu --speed 4000
