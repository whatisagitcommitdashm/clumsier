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
if not exist "build\baseline-msvc" mkdir "build\baseline-msvc"
if not exist "bin\baseline-msvc" mkdir "bin\baseline-msvc"
pushd etc
rc /nologo /d X64 /fo "..\build\baseline-msvc\clumsy.res" clumsy.rc
if errorlevel 1 exit /b 1
popd
cl /nologo /Zi /Od /MDd /W3 /D_DEBUG /D_CRT_SECURE_NO_WARNINGS /DX64 /Isrc /Isrc\core /Isrc\platform\windows /Isrc\backends\windows\legacy /Iexternal\iup-3.30_Win64_dll16_lib\include /Iexternal\WinDivert-2.2.0-A\include /Fo"build\baseline-msvc\\" /Fd"build\baseline-msvc\compiler.pdb" /Fe"bin\baseline-msvc\clumsy.exe" src\core\*.c src\ui\*.c src\backends\windows\*.c src\backends\windows\legacy\*.c src\platform\windows\*.c build\baseline-msvc\clumsy.res /link /DEBUG /INCREMENTAL:NO /MANIFEST:NO /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /PDB:"bin\baseline-msvc\clumsy.pdb" /LIBPATH:external\iup-3.30_Win64_dll16_lib /LIBPATH:external\WinDivert-2.2.0-A\x64 iup.lib WinDivert.lib comctl32.lib winmm.lib ws2_32.lib shell32.lib advapi32.lib user32.lib gdi32.lib comdlg32.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
copy /y external\iup-3.30_Win64_dll16_lib\iup.dll bin\baseline-msvc\ >nul
if errorlevel 1 exit /b 1
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll bin\baseline-msvc\ >nul
if errorlevel 1 exit /b 1
copy /y external\WinDivert-2.2.0-A\x64\WinDivert64.sys bin\baseline-msvc\ >nul
if errorlevel 1 exit /b 1
copy /y etc\config.txt bin\baseline-msvc\ >nul
if errorlevel 1 exit /b 1
echo Windows debug build completed. Application has not been launched.
