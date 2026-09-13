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
echo Building lifecycle tests for x64.
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /Zi /Od /MDd /W3 /D_CRT_SECURE_NO_WARNINGS /Iexternal\WinDivert-2.2.0-A\include /Isrc /Isrc\core /Isrc\platform\windows /Isrc\backends\windows\legacy /Fobuild\tests\ /Fdbuild\tests\compiler.pdb /Febuild\tests\lifecycle.exe tests\lifecycle.c src\backends\windows\legacy\packet.c /link /DEBUG /MACHINE:X64 /LIBPATH:external\WinDivert-2.2.0-A\x64 WinDivert.lib ws2_32.lib
if errorlevel 1 exit /b 1
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll build\tests\ >nul
if errorlevel 1 exit /b 1
build\tests\lifecycle.exe
exit /b %errorlevel%
