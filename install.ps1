param([string]$GameDir)
$ErrorActionPreference = "Stop"

function Find-GameDir {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { $steam = "${env:ProgramFiles(x86)}\Steam" }
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    $libs = @($steam)
    if (Test-Path $vdf) {
        $libs += Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' }
    }
    foreach ($l in $libs | Select-Object -Unique) {
        $d = Join-Path $l "steamapps\common\CONTROL Resonant"
        if (Test-Path "$d\CONTROLResonant.exe") { return $d }
    }
}

if (-not $GameDir) { $GameDir = Find-GameDir }
if (-not $GameDir -or -not (Test-Path "$GameDir\CONTROLResonant.exe")) {
    throw "CONTROLResonant.exe not found. Run: install.ps1 -GameDir 'X:\path\to\CONTROL Resonant'"
}

# Works both from the repo (build\version.dll) and from the release zip (version.dll next to the script)
$dll = @("$PSScriptRoot\version.dll", "$PSScriptRoot\build\version.dll") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dll) { throw "version.dll not found - download the release zip or run build.bat" }

$dst = "$GameDir\version.dll"
if ((Test-Path $dst) -and -not (Test-Path "$GameDir\audiofix.ini")) {
    throw "Another mod already uses version.dll in $GameDir - refusing to overwrite it."
}
Copy-Item $dll $dst -Force
if (-not (Test-Path "$GameDir\audiofix.ini")) { Copy-Item "$PSScriptRoot\audiofix.ini" $GameDir }
Write-Host "Installed to $GameDir"
