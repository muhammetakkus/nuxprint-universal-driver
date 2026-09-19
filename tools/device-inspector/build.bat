@echo off
REM Developer Command Prompt for VS (x64) icinde calistirin.
REM WDK gerekmez; yalnizca Windows SDK yeterlidir.
setlocal
cl /nologo /EHsc /W4 /DUNICODE /D_UNICODE /Fe:NuxPrintDeviceInspector.exe main.cpp ^
   /link setupapi.lib winspool.lib
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo OK -^> NuxPrintDeviceInspector.exe
