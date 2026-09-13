# Shared discovery for the double-click builder and release packager.
function Find-ClumsierBuildTools {
    param([string]$QtRoot, [switch]$Interactive)
    $repo = Split-Path $PSScriptRoot -Parent
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (!$vs) {
        if ($Interactive) {
            [System.Windows.Forms.MessageBox]::Show('Install Visual Studio 2022 Build Tools. Select Desktop development with C++, including the Windows SDK and C++ CMake tools. Then double-click Build Clumsier again.', 'C++ tools needed') | Out-Null
            Start-Process 'https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022'
        }
        throw 'Visual Studio C++ Build Tools were not found.'
    }
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
    if (!$cmake) {
        $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
    if (!(Test-Path -LiteralPath $cmake)) {
        if ($Interactive) {
            [System.Windows.Forms.MessageBox]::Show('Install CMake with its Add to PATH option, or add C++ CMake tools through the Visual Studio Installer. Then run this builder again.', 'CMake needed') | Out-Null
            Start-Process 'https://cmake.org/download/'
        }
        throw 'CMake was not found.'
    }
    $candidates = @($QtRoot, $env:CLUMSIER_QT_ROOT, "$repo/bin/Qt/6.10.3/msvc2022_64")
    foreach ($base in @('C:/Qt', "$env:USERPROFILE/Qt")) {
        if (Test-Path -LiteralPath $base) {
            $candidates += @(Get-ChildItem -LiteralPath $base -Directory | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'msvc2022_64' })
        }
    }
    $chosen = $null
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath "$candidate/bin/windeployqt.exe") -and (Test-Path -LiteralPath "$candidate/lib/cmake/Qt6QuickControls2")) {
            $chosen = (Resolve-Path -LiteralPath $candidate).Path
            break
        }
    }
    if (!$chosen -and $Interactive) {
        [System.Windows.Forms.MessageBox]::Show('Select your Qt MSVC 2022 64-bit kit folder (for example C:\Qt\6.10.3\msvc2022_64). If Qt is not installed, cancel to open the official download page. Install Qt 6.10.3 MSVC 2022 64-bit, then run this builder again.', 'Find Qt') | Out-Null
        $picker = New-Object System.Windows.Forms.FolderBrowserDialog
        $picker.Description = 'Choose the msvc2022_64 Qt kit folder'
        if ($picker.ShowDialog() -eq 'OK') { $chosen = $picker.SelectedPath }
        else { Start-Process 'https://www.qt.io/development/download-open-source' }
        $picker.Dispose()
    }
    if (!$chosen -or !(Test-Path -LiteralPath "$chosen/bin/windeployqt.exe") -or !(Test-Path -LiteralPath "$chosen/lib/cmake/Qt6QuickControls2")) {
        throw 'Qt MSVC 2022 x64 with Quick Controls was not found. See docs/BUILDING.md.'
    }
    $version = (& "$chosen/bin/qmake.exe" -query QT_VERSION | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -notmatch '^6\.\d+\.\d+$' -or [version]$version -lt [version]'6.8.0') {
        throw 'Clumsier requires Qt 6.8 or newer; this beta is packaged with Qt 6.10.3.'
    }
    [pscustomobject]@{ QtRoot = $chosen; QtVersion = $version; CMake = $cmake; VisualStudio = $vs }
}
