param([Parameter(Mandatory=$true)][string]$NativeDirectory,[ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$NativeDirectory=[IO.Path]::GetFullPath($NativeDirectory)
$output=Join-Path $NativeDirectory 'managed'
New-Item -ItemType Directory -Force $output|Out-Null
$dotnet=(Get-Command dotnet.exe -ErrorAction Stop).Source
$sdk=@(& $dotnet --list-sdks|Where-Object {$_ -match '^10\.'})
if($sdk.Count){
    & $dotnet build (Join-Path $PSScriptRoot 'Integration/Integration.csproj') -c $Configuration -o $output --nologo
    if($LASTEXITCODE){throw '.NET SDK build failed'}
}else{
    # Runtime-only machines can validate with the installed VS Roslyn compiler.
    # CI uses the .NET SDK/reference packs and performs the ordinary project build.
    $vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -property installationPath
    $compiler=Join-Path $vs 'MSBuild/Current/Bin/Roslyn/csc.exe'
    $runtime=Get-ChildItem (Join-Path (Split-Path $dotnet -Parent) 'shared/Microsoft.NETCore.App') -Directory|Where-Object Name -Like '10.*'|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1
    if(-not $runtime -or -not(Test-Path $compiler)){throw 'Install .NET 10 SDK, or VS Roslyn plus a .NET 10 runtime.'}
    $references=@(Get-ChildItem $runtime.FullName -Filter '*.dll'|ForEach-Object {try{[void][Reflection.AssemblyName]::GetAssemblyName($_.FullName);'/reference:"'+$_.FullName+'"'}catch{}})
    $options=@('/nostdlib+','/nullable:enable','/langversion:latest','/platform:x64','/warnaserror+')
    $library=Join-Path $output 'IpcRevival.dll'
    $rsp=Join-Path $output 'library.rsp'
    @($options+$references+@('/target:library',('/out:"'+$library+'"'),('"'+(Join-Path $PSScriptRoot 'IpcRevival/ChecksumClient.cs')+'"')))|Set-Content $rsp
    & $compiler /noconfig ("@"+$rsp)
    if($LASTEXITCODE){throw 'Runtime-reference library build failed'}
    $rsp=Join-Path $output 'integration.rsp'
    @($options+$references+@('/target:exe',('/reference:"'+$library+'"'),('/out:"'+(Join-Path $output 'Integration.dll')+'"'),('"'+(Join-Path $PSScriptRoot 'Integration/Program.cs')+'"')))|Set-Content $rsp
    & $compiler /noconfig ("@"+$rsp)
    if($LASTEXITCODE){throw 'Runtime-reference integration build failed'}
    '{"runtimeOptions":{"tfm":"net10.0","framework":{"name":"Microsoft.NETCore.App","version":"10.0.0"}}}'|Set-Content (Join-Path $output 'Integration.runtimeconfig.json')
    Write-Output 'Local .NET validation uses Roslyn/runtime assemblies; SDK project validation is a separate CI gate.'
}
Copy-Item (Join-Path $NativeDirectory 'ipc_bridge.dll'),(Join-Path $NativeDirectory 'ipc_broker.exe') -Destination $output -Force
Write-Output $output
