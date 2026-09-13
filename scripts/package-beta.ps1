param([switch]$Preview, [string]$QtRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
. "$PSScriptRoot/windows-tools.ps1"
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Write-Utf8($path, $text) { [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding $false)) }
function Write-Checksums($folder) {
    $lines = @(Get-ChildItem -LiteralPath $folder -File -Recurse | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($folder.Length + 1).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $relative
    })
    Write-Utf8 "$folder/SHA256SUMS.txt" (($lines -join "`n") + "`n")
}
$version = (Get-Content -LiteralPath "$repo/VERSION" -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+-beta\.\d+$') { throw 'VERSION must look like 0.1.0-beta.1.' }
$commit = (& git rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Run packaging from a Git checkout.' }
$dirty = [bool](& git status --porcelain --untracked-files=normal)
if ($dirty -and !$Preview) { throw 'Commit the reviewed changes first, or use -Preview for a clearly labeled rehearsal.' }
$tools = Find-ClumsierBuildTools -QtRoot $QtRoot
if ($tools.QtVersion -ne '6.10.3') { throw 'Release packaging pins Qt 6.10.3; other kits may be used for local development.' }
$label = "clumsier-$version"
if ($Preview) { $label += '-preview' }
$run = [guid]::NewGuid().ToString('N')
$stage = "$repo/work/release-builds/$run"
$source = "$stage/$label-source"
$windowsName = 'clumsier-beta-0.1'
$appFolder = "$stage/$windowsName/app"
$dependencies = "$stage/$label-dependency-sources"
$output = "$repo/dist/$label-$run"
foreach ($folder in @($source, $appFolder, $dependencies, $output, "$repo/work/release-cache")) {
    New-Item -ItemType Directory -Path $folder -Force | Out-Null
}

# Compile the same isolated source snapshot that we distribute. A local edit
# during compilation therefore cannot make the ZIP disagree with the binary.
if (!$Preview) {
    & git archive --format=tar "--output=$stage/committed-source.tar" $commit
    if ($LASTEXITCODE -ne 0) { throw 'Could not snapshot the release commit.' }
    & tar.exe -xf "$stage/committed-source.tar" -C $source
    if ($LASTEXITCODE -ne 0) { throw 'Could not extract the release commit.' }
}
$files = if ($Preview) { @(& git -c core.quotepath=false ls-files --cached --others --exclude-standard | Sort-Object -Unique) } else { @() }
if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate source files.' }
foreach ($relative in $files) {
    $path = Join-Path $repo $relative
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { continue } # Retired files in a preview.
    $item = Get-Item -LiteralPath $path
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source symlinks are not supported: $relative" }
    $destination = Join-Path $source $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $path -Destination $destination
}
$info = [ordered]@{ version=$version; preview=[bool]$Preview; sourceCommit=$commit; uncommittedChanges=$dirty; builtUtc=[DateTime]::UtcNow.ToString('o'); qt=$tools.QtVersion; architecture='x64'; sourceArchive="$label-source.zip"; dependencyArchive="$label-dependency-sources.zip" }
Write-Utf8 "$source/BUILD-INFO.json" ($info | ConvertTo-Json)

$env:CLUMSIER_QT_ROOT = $tools.QtRoot
$env:CLUMSIER_CMAKE = $tools.CMake
$env:CLUMSIER_BUILD_DIR = "$stage/build"
$env:CLUMSIER_DEPLOY_DIR = $appFolder
$env:CLUMSIER_BUILD_SCRIPT = "$source/scripts/build-qt-beta.cmd"
& $env:ComSpec /d /c 'call "%CLUMSIER_BUILD_SCRIPT%" 2>&1' | Tee-Object -FilePath "$output/build.log"
if ($LASTEXITCODE -ne 0) { throw "Build failed; see $output/build.log" }
$ctest = Join-Path (Split-Path $tools.CMake -Parent) 'ctest.exe'
$env:PATH = "$($tools.QtRoot)/bin;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:CLUMSIER_CTEST = $ctest
& $env:ComSpec /d /c '"%CLUMSIER_CTEST%" --test-dir "%CLUMSIER_BUILD_DIR%" --output-on-failure 2>&1' | Tee-Object -FilePath "$output/tests.log"
if ($LASTEXITCODE -ne 0) { throw "Tests failed; see $output/tests.log" }

$origins = Get-Content -LiteralPath "$source/packaging/dependency-sources.json" -Raw | ConvertFrom-Json
foreach ($origin in $origins) {
    $cached = "$repo/work/release-cache/$($origin.file)"
    if (!(Test-Path -LiteralPath $cached)) {
        Write-Host "Downloading source: $($origin.file)"
        Invoke-WebRequest -UseBasicParsing -Uri $origin.url -OutFile $cached
    }
    if ((Get-FileHash -LiteralPath $cached).Hash -ne $origin.sha256) { throw "Source checksum mismatch: $($origin.file)" }
    Copy-Item -LiteralPath $cached -Destination $dependencies
}
Copy-Item -LiteralPath "$source/packaging/dependency-sources.json" -Destination $dependencies
# Retain Qt's complete license directory, not just the LGPL heading.
foreach ($module in @('qtbase','qtdeclarative','qttools')) {
    $extract = "$stage/licenses-$module"
    New-Item -ItemType Directory -Path $extract | Out-Null
    & tar.exe -xf "$dependencies/$module-everywhere-src-6.10.3.tar.xz" -C $extract "$module-everywhere-src-6.10.3/LICENSES"
    if ($LASTEXITCODE -ne 0) { throw "Could not extract $module license texts." }
    New-Item -ItemType Directory -Force -Path "$appFolder/licenses" | Out-Null
    Copy-Item -LiteralPath "$extract/$module-everywhere-src-6.10.3/LICENSES" -Destination "$appFolder/licenses/$module" -Recurse
}
Copy-Item -LiteralPath "$($tools.QtRoot)/sbom" -Destination "$appFolder/licenses/qt-sbom" -Recurse
Copy-Item -LiteralPath "$source/packaging/START-HERE.txt", "$source/packaging/THIRD-PARTY-NOTICES.txt", "$source/BUILD-INFO.json" -Destination $appFolder
foreach ($required in @('clumsier-beta.exe','WinDivert.dll','WinDivert64.sys','Qt6Core.dll','platforms/qwindows.dll','vc_redist.x64.exe')) {
    if (!(Test-Path -LiteralPath "$appFolder/$required")) { throw "Missing packaged dependency: $required" }
}
if (Get-ChildItem -LiteralPath $appFolder -Recurse -File | Where-Object { $_.Name -match '^(hotkeys|preferences|sequence-servers)\.ini$|test.*\.exe$|\.pdb$' }) {
    throw 'Unexpected development files or personal settings in package.'
}
Write-Checksums $source
Copy-Item -LiteralPath "$stage/build/clumsier-launcher.exe" -Destination "$stage/$windowsName/clumsier-beta.exe"
# Include the launcher in the manifest while keeping the manifest in app/.
Write-Checksums $appFolder
Add-Content -LiteralPath "$appFolder/SHA256SUMS.txt" -Value ((Get-FileHash -LiteralPath "$stage/$windowsName/clumsier-beta.exe").Hash.ToLowerInvariant() + '  ../clumsier-beta.exe')
Write-Checksums $dependencies
& "$source/scripts/check-package.ps1" -Folder "$stage/$windowsName"
foreach ($folder in @($source, "$stage/$windowsName", $dependencies)) {
    [IO.Compression.ZipFile]::CreateFromDirectory($folder, "$output/$(Split-Path $folder -Leaf).zip", [IO.Compression.CompressionLevel]::Optimal, $true)
}
Write-Checksums $output
Write-Host "Release files: $output"
Write-Host 'Upload all three ZIPs plus SHA256SUMS.txt together after final review.'
