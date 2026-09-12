@echo off
setlocal
cd /d "%~dp0.."
rem Set CLUMSIER_QT_ROOT to use an SDK installed elsewhere.
if not defined CLUMSIER_QT_ROOT set "CLUMSIER_QT_ROOT=%CD%\bin\Qt\6.10.3\msvc2022_64"
if not exist "%CLUMSIER_QT_ROOT%\bin\windeployqt.exe" (
  echo Qt SDK not found at %CLUMSIER_QT_ROOT%
  echo Set CLUMSIER_QT_ROOT to your Qt MSVC 2022 x64 kit directory.
  exit /b 1
)
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" exit /b 1
for /f "usebackq tokens=*" %%I in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "clumsierVS=%%I"
if not defined clumsierVS exit /b 1
call "%clumsierVS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cmake -S src/ui/qt -B build/pre-beta -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CLUMSIER_QT_ROOT%" -DBUILD_TESTING=OFF
if errorlevel 1 exit /b 1
cmake --build build/pre-beta --target clumsier-ui-preview --parallel
if errorlevel 1 exit /b 1
if not exist bin\pre-beta mkdir bin\pre-beta
copy /y build\pre-beta\clumsier-ui-preview.exe bin\pre-beta\clumsier-pre-beta.exe >nul
if errorlevel 1 exit /b 1
"%CLUMSIER_QT_ROOT%\bin\windeployqt.exe" --release --no-translations --qmldir src/ui/qt bin/pre-beta/clumsier-pre-beta.exe
if errorlevel 1 exit /b 1
copy /y LICENSE bin\pre-beta\Clumsier-LICENSE.txt >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll bin\pre-beta\ >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert64.sys bin\pre-beta\ >nul
copy /y external\WinDivert-2.2.0-A\LICENSE bin\pre-beta\WinDivert-LICENSE.txt >nul
copy /y external\cjson\LICENSE bin\pre-beta\cJSON-LICENSE.txt >nul
copy /y third_party\tinted-schemes\LICENSE bin\pre-beta\Tinted-Theming-LICENSE.txt >nul
if errorlevel 1 exit /b 1
echo Clumsier pre-beta built: bin\pre-beta\clumsier-pre-beta.exe
echo Windows will request administrator permission on launch. Capture begins only when Start is pressed.
