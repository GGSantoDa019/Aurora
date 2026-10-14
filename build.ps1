param([string]$ToolchainBin = '')
$ErrorActionPreference = 'Stop'
if ($ToolchainBin) { $env:PATH = $ToolchainBin + ';' + $env:PATH }
$project = $PSScriptRoot
$build = Join-Path $project 'build'
if ($ToolchainBin) {
    & cmake -S $project -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
} else {
    & cmake -S $project -B $build
}
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $build --config Release --target Aurora_VST3 AuroraFX_VST3 Aurora_Standalone AuroraTests --parallel 3
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
Write-Host "Binaries are in $build; run AuroraTests from the project parent directory."
