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
cmake -S src/ui/qt -B build/qt-preview -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CLUMSIER_QT_ROOT%" -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
cmake --build build/qt-preview --parallel
if errorlevel 1 exit /b 1
if not exist bin\qt-preview mkdir bin\qt-preview
copy /y build\qt-preview\clumsier-ui-preview.exe bin\qt-preview\ >nul
if errorlevel 1 exit /b 1
"%CLUMSIER_QT_ROOT%\bin\windeployqt.exe" --release --no-translations --qmldir src/ui/qt bin/qt-preview/clumsier-ui-preview.exe
if errorlevel 1 exit /b 1
copy /y LICENSE bin\qt-preview\Clumsier-LICENSE.txt >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll bin\qt-preview\ >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert64.sys bin\qt-preview\ >nul
copy /y external\WinDivert-2.2.0-A\LICENSE bin\qt-preview\WinDivert-LICENSE.txt >nul
copy /y external\cjson\LICENSE bin\qt-preview\cJSON-LICENSE.txt >nul
copy /y third_party\tinted-schemes\LICENSE bin\qt-preview\Tinted-Theming-LICENSE.txt >nul
if errorlevel 1 exit /b 1
echo Qt UI preview built: bin\qt-preview\clumsier-ui-preview.exe
echo Windows will request administrator permission on launch. Capture begins only when Start is pressed.
