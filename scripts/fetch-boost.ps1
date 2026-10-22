param([string]$Destination='')
$ErrorActionPreference='Stop'
if(-not $Destination){$Destination=Join-Path (Split-Path $PSScriptRoot -Parent) '.deps'}
$Destination=[IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination|Out-Null
$archive=Join-Path $Destination 'boost-1.92.0.tar.gz'
$expected='e2a814b3a158ab482c7a3d330f8bf5a7a8423d258a4e2fb396996e00fcee2111'
if(-not (Test-Path -LiteralPath $archive)){
    Invoke-WebRequest -Uri 'https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-b2-nodocs.tar.gz' -OutFile $archive
}
if((Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant() -ne $expected){throw 'Boost archive checksum mismatch; remove/replace this specific cached file before retrying.'}
$boost=Join-Path $Destination 'boost-1.92.0'
if(-not (Test-Path -LiteralPath (Join-Path $boost 'boost/version.hpp'))){
    & tar -xf $archive -C $Destination
    if($LASTEXITCODE){throw 'Boost extraction failed'}
}
Write-Output $boost
