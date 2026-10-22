$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($env:GITHUB_REPOSITORY-ne'SeanShaffer88/ipc-revival'-or $env:GITHUB_REF-ne'refs/heads/master'-or $env:GITHUB_EVENT_NAME-ne'push'){throw 'Publishing is restricted to this project master push workflow'}
$release=Get-Content -Raw (Join-Path $root 'release.json')|ConvertFrom-Json
if(-not $release.ready){Write-Output 'Release gate remains closed';exit 0}
if($release.version -notmatch '^\d+\.\d+\.\d+$'){throw 'Invalid stable version'}
$commit=[string](git -C $root rev-parse HEAD)
if($commit-ne$env:GITHUB_SHA){throw 'Checkout does not match workflow commit'}
$sourceHash=& (Join-Path $PSScriptRoot 'source-hash.ps1') -Root $root
$evidence=@()
$legacy=Get-Content -Raw (Join-Path $root '.build/validated/validation.json')|ConvertFrom-Json
if($legacy.commit-ne$commit-or -not $legacy.passed-or $legacy.legacyManagedCases-ne5){throw 'Historical managed validation is missing or stale'}
foreach($config in @('Debug','Release')){
    $bundle=Join-Path $root ('.build/validated/'+$config)
    $entry=Get-Content -Raw (Join-Path $bundle 'validation.json')|ConvertFrom-Json
    if($entry.commit-ne$commit-or $entry.sourceHash-ne$sourceHash-or -not $entry.testsPassed-or -not $entry.managedSdkBuild-or $entry.upstreamNativeCases-ne1915-or $entry.ctestSuites-ne5){throw "Invalid $config validation evidence"}
    foreach($line in Get-Content (Join-Path $bundle 'SHA256SUMS.txt')){
        if($line-notmatch'^([a-f0-9]{64})  ([A-Za-z0-9_.-]+)$'){throw 'Invalid bundle checksum entry'}
        $hash=$matches[1];$file=$matches[2]
        if((Get-FileHash (Join-Path $bundle $file)).Hash.ToLowerInvariant()-ne$hash){throw "Bundle checksum mismatch: $file"}
    }
    $evidence+=$entry
}
$destination=Join-Path $root '.build/publish';New-Item -ItemType Directory -Force $destination|Out-Null
$prefix='ipc-revival-'+$release.version
$source=Join-Path $destination ($prefix+'-source.zip');$binaries=Join-Path $destination ($prefix+'-win-x64.zip')
git -C $root archive --format=zip ("--output="+$source) HEAD
if($LASTEXITCODE){throw 'Source archive failed'}
Compress-Archive -Path (Join-Path $root '.build/validated/Release/*') -DestinationPath $binaries
$validation=Join-Path $destination ($prefix+'-validation.json')
$ci='https://github.com/'+$env:GITHUB_REPOSITORY+'/actions/runs/'+$env:GITHUB_RUN_ID
[ordered]@{version=$release.version;commit=$commit;ci=$ci;targets=$evidence;legacy=$legacy;scope='Windows x64 native core and .NET 10 checksum bridge; historical C++/CLI compatibility tested separately and not bundled'}|ConvertTo-Json -Depth 8|Set-Content $validation
$sums=Join-Path $destination ($prefix+'-SHA256SUMS.txt')
@($source,$binaries,$validation)|ForEach-Object{(Get-FileHash $_).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($_)}|Set-Content $sums -Encoding ASCII
$tag='v'+$release.version
$existing=& gh api ('repos/'+$env:GITHUB_REPOSITORY+'/releases') --paginate
if($LASTEXITCODE){throw 'Could not check existing releases'}
if(@($existing|ConvertFrom-Json|Where-Object tag_name -eq $tag).Count){throw 'Release/tag already has a release; inspect it rather than overwrite assets'}
$notes="Restored Windows x64 native IPC core and process-isolated .NET 10 checksum client. Both peers require the revival ABI/Boost/configuration. 1,915 upstream native cases per Debug/Release configuration, resource failures, native process tests and managed crash/poisoned-lock tests passed. Historical C++/CLI compatibility tests passed separately ($($legacy.legacyManagedCases) NUnit cases); those assemblies are not bundled. Native raw shared allocators are not owner-death robust; the bridge kills a poisoned broker on deadline and does not repair a corrupt segment. See REVIEW.md and README.revival.md. Exact commit: $commit. CI: $ci. Requires Windows, .NET 10 for managed clients and MSVC x64 runtime. MIT/Boost licenses retained."
$body=Join-Path $destination 'create-release.json'
@{tag_name=$tag;target_commitish=$commit;name=('IPC revival '+$release.version);body=$notes;draft=$true;prerelease=$false}|ConvertTo-Json|Set-Content $body
$created=& gh api -X POST ('repos/'+$env:GITHUB_REPOSITORY+'/releases') --input $body
if($LASTEXITCODE){throw 'Draft release creation failed'}
$draft=$created|ConvertFrom-Json
$assets=@($source,$binaries,$validation,$sums)
& gh release upload $tag @assets --repo $env:GITHUB_REPOSITORY
if($LASTEXITCODE){throw 'Release upload failed; draft retained'}
$remote=& gh api ('repos/'+$env:GITHUB_REPOSITORY+'/releases/'+$draft.id)
if($LASTEXITCODE){throw 'Asset verification API failed'}
$remote=$remote|ConvertFrom-Json
foreach($file in $assets){$asset=$remote.assets|Where-Object name -eq ([IO.Path]::GetFileName($file));$hash=(Get-FileHash $file).Hash.ToLowerInvariant();if(-not $asset-or $asset.state-ne'uploaded'-or $asset.digest-ne('sha256:'+$hash)-or $asset.size-ne(Get-Item $file).Length){throw 'Uploaded asset verification failed; draft retained'}}
$body=Join-Path $destination 'publish-release.json';'{"draft":false,"make_latest":"true"}'|Set-Content $body
& gh api -X PATCH ('repos/'+$env:GITHUB_REPOSITORY+'/releases/'+$draft.id) --input $body
if($LASTEXITCODE){throw 'Publication failed; inspect draft'}
