param(
    [Parameter(Mandatory=$true)][string]$BoostRoot,
    [ValidateSet('Debug','Release')][string]$Configuration='Release',
    [string]$Generator='',
    [string]$BuildDirectory=''
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$initialSourceHash=& (Join-Path $PSScriptRoot 'source-hash.ps1') -Root $root
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'Install Visual Studio C++ Build Tools with CMake support.'}
$cmake=Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$build=if($BuildDirectory){[IO.Path]::GetFullPath($BuildDirectory)}else{Join-Path $root '.build/x64'}
if(-not $Generator){$major=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Split('.')[0]; $Generator=if($major -eq '18'){'Visual Studio 18 2026'}else{'Visual Studio 17 2022'}}
& $cmake -S $root -B $build -G $Generator -A x64 ("-DIPC_BOOST_ROOT="+[IO.Path]::GetFullPath($BoostRoot))
if($LASTEXITCODE){throw 'CMake configure failed'}
& $cmake --build $build --config $Configuration --parallel 1 -- /nodeReuse:false
if($LASTEXITCODE){throw 'Build failed'}
& (Join-Path $root 'managed/build.ps1') -NativeDirectory (Join-Path $build $Configuration) -Configuration $Configuration
$ctest=Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
& $ctest --test-dir $build -C $Configuration --output-on-failure --output-junit (Join-Path $build ("ctest-"+$Configuration+'.xml'))
if($LASTEXITCODE){throw 'Tests failed'}
$testLog=Get-Content -Raw (Join-Path $build 'Testing/Temporary/LastTest.log')
if($testLog -notmatch 'Running (\d+) test cases' -or [int]$matches[1] -ne 1915){throw 'Unexpected upstream test inventory'}
$upstreamCases=[int]$matches[1]
if($initialSourceHash-ne(& (Join-Path $PSScriptRoot 'source-hash.ps1') -Root $root)){throw 'Source changed during validation; no evidence will be recorded'}
$evidence=[ordered]@{commit=([string](git -C $root rev-parse HEAD));sourceHash=$initialSourceHash;configuration=$Configuration;architecture='x64';upstreamNativeCases=$upstreamCases;ctestSuites=5;managedSdkBuild=([bool]@(& dotnet --list-sdks|Where-Object {$_ -match '^10\.'}).Count);testsPassed=$true;utc=[DateTime]::UtcNow.ToString('o')}
$evidence|ConvertTo-Json|Set-Content (Join-Path $build ("build-evidence-"+$Configuration+'.json'))
