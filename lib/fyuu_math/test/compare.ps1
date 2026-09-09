param([string]$Compiler = 'clang++')
$ErrorActionPreference = 'Stop'
$source = Split-Path $PSScriptRoot -Parent
foreach ($mode in @('ON', 'OFF')) {
    $build = Join-Path $source "../../cmake-build/fyuu-math-compare-$mode"
    & cmake -S $source -B $build -G Ninja "-DCMAKE_CXX_COMPILER=$Compiler" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DFYUU_MATH_ENABLE_SIMD=$mode"
    if ($LASTEXITCODE) { throw "Configure failed: $mode" }
    & cmake --build $build
    if ($LASTEXITCODE) { throw "Build failed: $mode" }
    & ctest --test-dir $build -V
    if ($LASTEXITCODE) { throw "Tests failed: $mode" }
}
