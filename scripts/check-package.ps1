param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference = 'Stop'
$folderPath = (Resolve-Path -LiteralPath $Folder).Path
$manifestRoot = if (Test-Path -LiteralPath "$folderPath/app/SHA256SUMS.txt") { "$folderPath/app" } else { $folderPath }
foreach ($line in Get-Content -LiteralPath "$manifestRoot/SHA256SUMS.txt") {
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Malformed package checksum entry.' }
    $expected = $Matches[1]
    $path = [IO.Path]::GetFullPath((Join-Path $manifestRoot $Matches[2]))
    if (!$path.StartsWith($folderPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Checksum path escapes the package.' }
    if ((Get-FileHash -LiteralPath $path).Hash -ne $expected) { throw "Checksum mismatch: $path" }
}
# Check only QML/plugin loading, never open capture. The executable's dedicated
# check mode creates no backend and does not show an application window.
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = "$folderPath/clumsier-beta.exe"
$start.Arguments = '--check-load'
$start.WorkingDirectory = $folderPath
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
$start.EnvironmentVariables['__COMPAT_LAYER'] = 'RunAsInvoker'
foreach ($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) { $start.EnvironmentVariables.Remove($key) }
$process = New-Object Diagnostics.Process
$process.StartInfo = $start
if (!$process.Start()) { throw 'Could not start the package check.' }
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
if (!$process.WaitForExit(45000)) { $process.Kill(); throw 'Package load check timed out.' }
$exitCode = $process.ExitCode
$output = $stdout.Result + $stderr.Result
$process.Dispose()
if ($exitCode -ne 0 -or $output -match 'failed to load|is not installed|is not a type|ReferenceError|TypeError') {
    throw "Package load check failed ($exitCode): $output"
}
Write-Host 'PASS: package checksums and embedded QML load with Qt SDK paths removed.'
Write-Host 'This does not replace testing runtime installation and capture on a clean Windows machine.'
