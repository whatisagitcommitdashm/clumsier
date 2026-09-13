@echo off
setlocal
cd /d "%~dp0.."
if not defined CLUMSIER_CMAKE set "CLUMSIER_CMAKE=cmake"
if not defined CLUMSIER_BUILD_DIR set "CLUMSIER_BUILD_DIR=%CD%\build\qt-beta"
if not defined CLUMSIER_DEPLOY_DIR set "CLUMSIER_DEPLOY_DIR=%CD%\bin\qt-beta"
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
"%CLUMSIER_CMAKE%" -S src/ui/qt -B "%CLUMSIER_BUILD_DIR%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CLUMSIER_QT_ROOT%" -DBUILD_TESTING=ON
if errorlevel 1 exit /b 1
"%CLUMSIER_CMAKE%" --build "%CLUMSIER_BUILD_DIR%" --parallel
if errorlevel 1 exit /b 1
if not exist "%CLUMSIER_DEPLOY_DIR%" mkdir "%CLUMSIER_DEPLOY_DIR%"
copy /y "%CLUMSIER_BUILD_DIR%\clumsier-beta.exe" "%CLUMSIER_DEPLOY_DIR%\" >nul
if errorlevel 1 exit /b 1
"%CLUMSIER_QT_ROOT%\bin\windeployqt.exe" --release --no-translations --qmldir src/ui/qt "%CLUMSIER_DEPLOY_DIR%\clumsier-beta.exe"
if errorlevel 1 exit /b 1
copy /y LICENSE "%CLUMSIER_DEPLOY_DIR%\Clumsier-LICENSE.txt" >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert.dll "%CLUMSIER_DEPLOY_DIR%\" >nul
copy /y external\WinDivert-2.2.0-A\x64\WinDivert64.sys "%CLUMSIER_DEPLOY_DIR%\" >nul
copy /y external\WinDivert-2.2.0-A\LICENSE "%CLUMSIER_DEPLOY_DIR%\WinDivert-LICENSE.txt" >nul
copy /y external\cjson\LICENSE "%CLUMSIER_DEPLOY_DIR%\cJSON-LICENSE.txt" >nul
copy /y third_party\tinted-schemes\LICENSE "%CLUMSIER_DEPLOY_DIR%\Tinted-Theming-LICENSE.txt" >nul
if errorlevel 1 exit /b 1
echo Qt beta built: %CLUMSIER_DEPLOY_DIR%\clumsier-beta.exe
echo Windows will request administrator permission on launch. Capture begins only when Start is pressed.
