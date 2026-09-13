param([switch]$Interactive, [string]$QtRoot, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
if ($Interactive) { Add-Type -AssemblyName System.Windows.Forms }
. "$PSScriptRoot/windows-tools.ps1"
try {
    $tools = Find-ClumsierBuildTools -QtRoot $QtRoot -Interactive:$Interactive
    Write-Host "Qt $($tools.QtVersion): $($tools.QtRoot)"
    Write-Host "CMake: $($tools.CMake)"
    if ($CheckOnly) { exit 0 }
    $env:CLUMSIER_QT_ROOT = $tools.QtRoot
    $env:CLUMSIER_CMAKE = $tools.CMake
    New-Item -ItemType Directory -Force -Path work | Out-Null
    # Keep the log available after the Explorer-launched window closes.
    $env:CLUMSIER_BUILD_SCRIPT = "$PSScriptRoot/build-qt-beta.cmd"
    & $env:ComSpec /d /c 'call "%CLUMSIER_BUILD_SCRIPT%" 2>&1' | Tee-Object -FilePath work/build-from-source.log
    if ($LASTEXITCODE -ne 0) { throw 'Build failed. See work/build-from-source.log. Close Clumsier if its executable is locked.' }
    if ($Interactive) {
        [System.Windows.Forms.MessageBox]::Show('Build complete. Open clumsier-beta.exe in the folder that opens next. Windows will request administrator permission when you launch the app.', 'Clumsier built') | Out-Null
        Invoke-Item -LiteralPath "$repo/bin/qt-beta"
    }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    if ($Interactive) { [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Clumsier build') | Out-Null }
    exit 1
}
