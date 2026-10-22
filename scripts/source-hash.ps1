param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
Push-Location $Root
try {
    $lines=foreach($path in @(git ls-files -co --exclude-standard|Sort-Object -Unique)){
        if(Test-Path -LiteralPath $path -PathType Leaf){$path+' '+(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    }
    $bytes=[Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))
    $sha=[Security.Cryptography.SHA256]::Create()
    try{[Convert]::ToHexString($sha.ComputeHash($bytes)).ToLowerInvariant()}finally{$sha.Dispose()}
}finally{Pop-Location}
