@echo off
setlocal
rem Select the target explicitly; the default developer prompt may target x86.
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
  echo Visual Studio Installer was not found. Install Visual Studio C++ Build Tools.
  exit /b 1
)
set "clumsierVS="
for /f "usebackq tokens=*" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "clumsierVS=%%I"
if not defined clumsierVS (
  echo No Visual Studio installation with the x64 C++ tools was found.
  exit /b 1
)
call "%clumsierVS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Failed to select the x64 compiler environment.
  exit /b 1
)
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DCJSON_NESTING_LIMIT=32 /Isrc /Fobuild\tests\ /Febuild\tests\core.exe tests\core.c src\core\*.c external\cjson\cJSON.c
if errorlevel 1 exit /b 1
build\tests\core.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W3 /D_CRT_SECURE_NO_WARNINGS /DCJSON_NESTING_LIMIT=32 /Isrc /Isrc\backends\windows\legacy /Iexternal\WinDivert-2.2.0-A\include /Fobuild\tests\ /Febuild\tests\windows-backend.exe tests\windows_backend.c src\backends\windows\backend.c src\backends\windows\legacy\packet.c src\core\network.c /link /LIBPATH:external\WinDivert-2.2.0-A\x64 WinDivert.lib ws2_32.lib
if errorlevel 1 exit /b 1
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll build\tests\ >nul
if errorlevel 1 exit /b 1
build\tests\windows-backend.exe
exit /b %errorlevel%
