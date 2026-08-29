$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir = Join-Path $Root "build"

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { throw "cmake not found. Install CMake and add it to PATH." }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found. Install VS 2022 C++ Build Tools." }

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio C++ x64 tools not found." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found under $vsPath" }

$cmakeExtra = "-DCMAKE_BUILD_TYPE=Release"
if ($ninja) { $cmakeExtra = "$cmakeExtra -DCMAKE_MAKE_PROGRAM=`"$ninja`"" }

$running = Get-Process -Name xnminer -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "xnminer.exe is running - stop it if the linker cannot overwrite build\bin\xnminer.exe"
}

Write-Host "Building xnminer (Windows C++ / CUDA) via MSVC + Ninja..."
$gen = if ($ninja) { "-G Ninja" } else { "" }
$cmd = "call `"$vcvars`" && cmake -S `"$Root`" -B `"$BuildDir`" $gen $cmakeExtra && cmake --build `"$BuildDir`" --config Release"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $BuildDir "bin\xnminer.exe"
if (-not (Test-Path $exe)) { throw "Build succeeded but $exe was not produced." }
Write-Host "Built: $exe"
Get-Item $exe | Format-List FullName, Length, LastWriteTime
exit 0
