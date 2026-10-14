@echo off
REM Developer Command Prompt for VS (x64) icinde calistirin.
REM WDK gerekmez; yalnizca Windows SDK yeterlidir.
setlocal
cl /nologo /std:c++17 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE /Fe:NuxPrintDeviceInspector.exe main.cpp ^
   /link setupapi.lib winspool.lib advapi32.lib
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo OK -^> NuxPrintDeviceInspector.exe
