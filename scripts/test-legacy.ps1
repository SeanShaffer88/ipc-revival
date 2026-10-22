$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try{
    New-Item -ItemType Directory -Force '.build/legacy'|Out-Null
    & nuget restore IPC.sln -NonInteractive
    if($LASTEXITCODE){throw 'Historical dependency restore failed'}
    $vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
    & $msbuild IPC.sln /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:TargetFrameworkVersion=v4.8 ("/p:ForceImportBeforeCppTargets="+(Join-Path $root 'compat/legacy.props')) /v:minimal
    if($LASTEXITCODE){throw 'Historical C++/CLI solution build failed'}
    & ./x64/Release/IPC.UnitTests.exe --report_level=short
    if($LASTEXITCODE){throw 'Historical native tests failed'}
    & nuget install NUnit.ConsoleRunner -Version 3.21.0 -OutputDirectory .deps/legacy-tools -NonInteractive -DirectDownload
    if($LASTEXITCODE){throw 'NUnit runner restore failed'}
    & ./.deps/legacy-tools/NUnit.ConsoleRunner.3.21.0/tools/nunit3-console.exe ./x64/Release/IPC.Managed.UnitTests.dll --workers=1 --timeout=30000 --result=.build/legacy/nunit.xml
    if($LASTEXITCODE){throw 'Historical managed tests failed'}
    $result=[xml](Get-Content -Raw '.build/legacy/nunit.xml')
    $testRun=$result.SelectSingleNode('/test-run')
    if($testRun.result-ne'Passed'-or[int]$testRun.total-le0-or[int]$testRun.failed-ne0){throw 'Invalid historical managed result'}
    [ordered]@{commit=([string](git rev-parse HEAD));legacyManagedCases=[int]$testRun.total;passed=$true;framework='net48 compatibility';boost='1.71 NuGet; compatibility lane only'}|ConvertTo-Json|Set-Content .build/legacy/validation.json
}finally{Pop-Location}
