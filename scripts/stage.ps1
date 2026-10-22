param([string]$BuildDirectory='',[ValidateSet('Debug','Release')][string]$Configuration='Release',[Parameter(Mandatory=$true)][string]$BoostRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $BuildDirectory){$BuildDirectory=Join-Path $root '.build/x64'}
$BuildDirectory=[IO.Path]::GetFullPath($BuildDirectory)
$evidence=Get-Content -Raw (Join-Path $BuildDirectory ("build-evidence-"+$Configuration+'.json'))|ConvertFrom-Json
if(-not $evidence.testsPassed -or $evidence.sourceHash -ne (& (Join-Path $PSScriptRoot 'source-hash.ps1') -Root $root)){throw 'Stale or failing build evidence'}
$bundle=Join-Path $root ('.build/bundle/'+$Configuration)
New-Item -ItemType Directory -Force $bundle|Out-Null
$native=Join-Path $BuildDirectory $Configuration
foreach($file in @('ipc_bridge.dll','ipc_broker.exe','ipc_worker.exe','ipc_calc.exe','ipc_native.lib')){Copy-Item -LiteralPath (Join-Path $native $file) -Destination $bundle -Force}
foreach($file in Get-ChildItem (Join-Path $native 'managed') -File|Where-Object {$_.Extension -in @('.dll','.json')}){Copy-Item -LiteralPath $file.FullName -Destination $bundle -Force}
Copy-Item -LiteralPath (Join-Path $root 'Bridge/ipc_bridge.h'),(Join-Path $root 'LICENSE'),(Join-Path $root 'REVIEW.md'),(Join-Path $root 'README.revival.md') -Destination $bundle -Force
Copy-Item -LiteralPath (Join-Path $BoostRoot 'LICENSE_1_0.txt') -Destination (Join-Path $bundle 'BOOST_LICENSE_1_0.txt') -Force
Copy-Item -LiteralPath (Join-Path $BuildDirectory ("build-evidence-"+$Configuration+'.json')) -Destination (Join-Path $bundle 'validation.json') -Force
Copy-Item -LiteralPath (Join-Path $BuildDirectory ("ctest-"+$Configuration+'.xml')) -Destination $bundle -Force
Copy-Item -LiteralPath (Join-Path $BuildDirectory ("process-results-"+$Configuration+'.json')) -Destination $bundle -Force
$hashes=foreach($file in Get-ChildItem $bundle -File|Where-Object Name -ne 'SHA256SUMS.txt'){(Get-FileHash $file.FullName).Hash.ToLowerInvariant()+'  '+$file.Name}
$hashes|Set-Content (Join-Path $bundle 'SHA256SUMS.txt') -Encoding ASCII
Write-Output $bundle
