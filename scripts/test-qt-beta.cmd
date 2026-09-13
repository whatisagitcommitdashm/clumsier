@echo off
setlocal
cd /d "%~dp0.."
if not defined CLUMSIER_QT_ROOT set "CLUMSIER_QT_ROOT=%CD%\bin\Qt\6.10.3\msvc2022_64"
if not exist build\qt-beta\qt-shell-test.exe (
  echo Run scripts\build-qt-beta.cmd first.
  exit /b 1
)
set "PATH=%CLUMSIER_QT_ROOT%\bin;%PATH%"
set "QT_FORCE_STDERR_LOGGING=1"
ctest --test-dir build/qt-beta --output-on-failure
exit /b %errorlevel%
