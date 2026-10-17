[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build-v03'
try {
    $running=Get-Process -ErrorAction Stop | Where-Object {$_.ProcessName -like 'Aether*' -or $_.ProcessName -like 'Retained*Recovery'}
    if ($running) {
        throw 'Close Aether UI first using its right-click menu. If a recovery test is still running, wait for its confirmed cleanup before rebuilding. A running executable cannot be replaced.'
    }
    $cmakeCommand=Get-Command cmake.exe -ErrorAction SilentlyContinue
    $cmake=if ($cmakeCommand) {$cmakeCommand.Source} else {$null}
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $installation=$null
    if (Test-Path -LiteralPath $vswhere) {
        $installation=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $cmake -and $installation) {
        $candidate=Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $candidate) {$cmake=$candidate}
    }
    if (-not $cmake) {throw 'CMake was not found. Visual Studio Installer > Modify > Desktop development with C++ > C++ CMake tools for Windows.'}
    Write-Host "Building Aether UI in $build" -ForegroundColor Cyan
    & $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) {throw 'CMake configuration failed. See the first error above.'}
    & $cmake --build $build --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) {throw 'Compilation failed. Do not launch an older executable as though it were this build.'}
    $ctest=Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest)) {throw 'ctest.exe was not found beside CMake.'}
    & $ctest --test-dir $build -C $Configuration --output-on-failure -j 1
    if ($LASTEXITCODE -ne 0) {throw 'At least one test failed. See the test output above.'}
    Write-Host "Build and tests passed. Executable: $build\$Configuration\AetherUI.exe" -ForegroundColor Green
    Write-Host 'Run Start.cmd to open the Release build. Nothing was added to Windows startup.'
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

