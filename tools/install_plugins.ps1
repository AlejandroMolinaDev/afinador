# Copies the built NovaPitch plugins to the standard Windows plugin folders so
# FL Studio finds them after "Find more plugins".
#
#   powershell -ExecutionPolicy Bypass -File tools\install_plugins.ps1 [-BuildDir build] [-Config Release] [-UserFolder]
#
# Default destinations (need an Administrator PowerShell):
#   C:\Program Files\Common Files\VST3\NovaPitch.vst3
#   C:\Program Files\Common Files\CLAP\NovaPitch.clap
# With -UserFolder (no admin): %USERPROFILE%\Documents\NovaPitch-dev\{VST3,CLAP}
#   -> add that folder in FL Studio: Options > Manage plugins > Plugin search paths.
param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$UserFolder
)

$ErrorActionPreference = "Stop"
$art = Join-Path $BuildDir "NovaPitch_artefacts\$Config"
if (-not (Test-Path $art)) {
    Write-Error "No artefacts found in $art. Build first: cmake --build $BuildDir --config $Config"
}

if ($UserFolder) {
    $root = Join-Path ([Environment]::GetFolderPath("MyDocuments")) "NovaPitch-dev"
    $vst3Dir = Join-Path $root "VST3"
    $clapDir = Join-Path $root "CLAP"
} else {
    $common = [Environment]::GetFolderPath("CommonProgramFiles")
    $vst3Dir = Join-Path $common "VST3"
    $clapDir = Join-Path $common "CLAP"
}

function Copy-Plugin($src, $dstDir) {
    if (Test-Path $src) {
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
        $dst = Join-Path $dstDir (Split-Path $src -Leaf)
        if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
        Copy-Item -Recurse -Force $src $dstDir
        Write-Host "  -> $dst"
    }
}

Write-Host "Installing NovaPitch ($Config) for Windows:"
Copy-Plugin (Join-Path $art "VST3\NovaPitch.vst3") $vst3Dir
Copy-Plugin (Join-Path $art "CLAP\NovaPitch.clap") $clapDir
Write-Host "Done. Close FL Studio before re-installing (it locks the DLL)."
Write-Host "In FL Studio: Options > Manage plugins > Find more plugins."
