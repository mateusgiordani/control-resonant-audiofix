param([string]$GameDir)
if (-not $GameDir) {
    $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    $libs = @($steam) + (Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' -ErrorAction SilentlyContinue | ForEach-Object { $_.Matches[0].Groups[1].Value -replace '\\\\', '\' })
    $GameDir = $libs | ForEach-Object { Join-Path $_ "steamapps\common\CONTROL Resonant" } | Where-Object { Test-Path "$_\CONTROLResonant.exe" } | Select-Object -First 1
}
if (-not $GameDir) { throw "Game folder not found. Run: uninstall.ps1 -GameDir 'X:\path\to\CONTROL Resonant'" }
if (-not (Test-Path "$GameDir\audiofix.ini")) { Write-Host "AudioFix is not installed in $GameDir"; return }
foreach ($f in "version.dll", "audiofix.ini", "audiofix.log") {
    $p = Join-Path $GameDir $f
    if (Test-Path $p) { Remove-Item $p -Force; Write-Host "removed $p" }
}
