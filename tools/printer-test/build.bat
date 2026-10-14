@echo off
REM Developer Command Prompt for VS (x64). Windows SDK yeterli, WDK gerekmez.
setlocal
cl /nologo /std:c++17 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /I..\.. ^
   /Fe:NuxPrintPrinterTest.exe ^
   main.cpp ..\..\core\escpos\escpos.cpp ..\..\core\raster\mono.cpp ..\..\core\raster\testpattern.cpp ^
   /link winspool.lib
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo OK -^> NuxPrintPrinterTest.exe
