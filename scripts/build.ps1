param(
    [Parameter(Mandatory=$true)][string]$BoostRoot,
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$Generator=''
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'Install Visual Studio C++ Build Tools with CMake support.'}
$cmake=Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build=Join-Path $root '.build/x64'
if(-not $Generator){$major=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Split('.')[0]; $Generator=if($major -eq '18'){'Visual Studio 18 2026'}else{'Visual Studio 17 2022'}}
& $cmake -S $root -B $build -G $Generator -A x64 ("-DIPC_BOOST_ROOT="+[IO.Path]::GetFullPath($BoostRoot))
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build $build --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'Build failed'}
& $cmake --build $build --config $Configuration --target RUN_TESTS
if($LASTEXITCODE){throw 'Tests failed'}
