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
echo Building action and hotkey tests for x64.
cd /d "%~dp0.."
if not exist build\tests mkdir build\tests
cl /nologo /Zi /Od /MDd /W3 /D_CRT_SECURE_NO_WARNINGS /Isrc /Isrc\core /Isrc\platform\windows /Isrc\backends\windows\legacy /Fobuild\tests\ /Fdbuild\tests\hotkeys-compiler.pdb /Febuild\tests\hotkeys.exe tests\hotkeys.c src\core\actions.c src\platform\windows\hotkey_settings.c src\core\hotkey_matcher.c /link /DEBUG /MACHINE:X64 user32.lib shell32.lib
if errorlevel 1 exit /b 1
build\tests\hotkeys.exe
if errorlevel 1 exit /b 1
cl /nologo /Zi /Od /MDd /W3 /D_CRT_SECURE_NO_WARNINGS /Isrc /Isrc\core /Isrc\platform\windows /Isrc\backends\windows\legacy /Fobuild\tests\ /Fdbuild\tests\listener-compiler.pdb /Febuild\tests\hotkey-listener.exe tests\hotkey_listener.c src\platform\windows\hotkeys.c src\core\actions.c src\platform\windows\hotkey_settings.c src\core\hotkey_matcher.c /link /DEBUG /MACHINE:X64 user32.lib shell32.lib
if errorlevel 1 exit /b 1
build\tests\hotkey-listener.exe
exit /b %errorlevel%
