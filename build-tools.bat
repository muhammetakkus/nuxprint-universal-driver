@echo off
REM ============================================================
REM  Her iki Windows aracini tek komutla derler.
REM
REM  ONEMLI: Bu dosyayi normal cmd'de degil,
REM  "Developer Command Prompt for VS 2022" icinde calistirin
REM  (cl.exe ancak orada PATH'te olur).
REM  Windows SDK yeterlidir; WDK bu asamada GEREKMEZ.
REM ============================================================
setlocal
where cl >nul 2>&1
if errorlevel 1 (
  echo.
  echo HATA: cl.exe bulunamadi.
  echo Baslat menusunden "Developer Command Prompt for VS 2022" acin
  echo ve bu dosyayi oradan calistirin.
  echo Visual Studio yoksa: "Build Tools for Visual Studio" + "Desktop development with C++"
  echo.
  exit /b 1
)

if not exist build mkdir build

echo [1/2] Device Inspector...
cl /nologo /EHsc /W4 /DUNICODE /D_UNICODE /Fo:build\ ^
   /Fe:build\NuxPrintDeviceInspector.exe ^
   tools\device-inspector\main.cpp ^
   /link setupapi.lib winspool.lib
if errorlevel 1 goto :fail

echo [2/2] Printer Test...
cl /nologo /EHsc /W4 /DUNICODE /D_UNICODE /I. /Fo:build\ ^
   /Fe:build\NuxPrintPrinterTest.exe ^
   tools\printer-test\main.cpp core\escpos\escpos.cpp core\raster\mono.cpp core\raster\testpattern.cpp ^
   /link winspool.lib
if errorlevel 1 goto :fail

echo.
echo OK
echo   build\NuxPrintDeviceInspector.exe
echo   build\NuxPrintPrinterTest.exe
exit /b 0

:fail
echo.
echo BUILD FAILED
exit /b 1
