param([Parameter(Mandatory=$true)][string]$NativeDirectory)
$ErrorActionPreference='Stop'
$NativeDirectory=[IO.Path]::GetFullPath($NativeDirectory)
$managed=Join-Path $NativeDirectory 'managed/Integration.dll'
$children=[Collections.Generic.List[object]]::new()
function Start-Child([string]$File,[string[]]$Arguments){
    $info=[Diagnostics.ProcessStartInfo]::new($File);$info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardInput=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
    $p=[Diagnostics.Process]::Start($info);$child=@{Process=$p;Errors=$p.StandardError.ReadToEndAsync()};$children.Add($child);return $child
}
function Server([string]$Name,[string]$Delay='0'){
    $child=Start-Child (Join-Path $NativeDirectory 'ipc_worker.exe') @('shm-server',$Name,$Delay)
    $ready=$child.Process.StandardOutput.ReadLineAsync();if(-not $ready.Wait(5000)-or $ready.Result-ne'READY'){throw 'Server not ready'};return $child
}
function Managed([string]$Mode,[string]$Name){return Start-Child (Get-Command dotnet.exe).Source @($managed,$Mode,$Name)}
function Finish($Child,[bool]$Success=$true){
    if(-not $Child.Process.WaitForExit(15000)){$Child.Process.Kill($true);throw 'Managed integration watchdog expired'}
    $output=$Child.Process.StandardOutput.ReadToEnd();$errors=$Child.Errors.GetAwaiter().GetResult()
    if(($Child.Process.ExitCode-eq0)-ne$Success){throw "Unexpected exit: $errors $output"};Write-Output $output
}
function Stop-Server($Child){$Child.Process.StandardInput.WriteLine('quit');Finish $Child}
try{
    $name='managed-'+[guid]::NewGuid().ToString('N');$server=Server $name
    Finish (Managed 'run' $name)
    $parallel=@(1..4|ForEach-Object {Managed 'run' $name})
    foreach($client in $parallel){Finish $client}
    Stop-Server $server
    $name='managed-'+[guid]::NewGuid().ToString('N');$server=Server $name '1500'
    Finish (Managed 'timeout' $name);Stop-Server $server
    $name='managed-'+[guid]::NewGuid().ToString('N');$server=Server $name
    $client=Managed 'peer' $name;$ready=$client.Process.StandardOutput.ReadLineAsync()
    if(-not $ready.Wait(8000)-or $ready.Result-ne'READY'){throw 'Managed peer not ready'}
    $server.Process.Kill($true);$server.Process.WaitForExit();$client.Process.StandardInput.WriteLine('continue');Finish $client
    $server=Server $name;Finish (Managed 'run' $name);Stop-Server $server
    # Poison a real shared allocator lock. The broker may hang during native
    # cleanup; the bridge must kill it and return to managed code on deadline.
    $name='poison-'+[guid]::NewGuid().ToString('N')
    $fixture=Start-Child (Join-Path $NativeDirectory 'ipc_crash_fixture.exe') @($name)
    $ready=$fixture.Process.StandardOutput.ReadLineAsync();if(-not $ready.Wait(5000)-or $ready.Result-ne'READY'){throw 'Crash fixture not ready'}
    $client=Managed 'peer' $name;$ready=$client.Process.StandardOutput.ReadLineAsync()
    if(-not $ready.Wait(8000)-or $ready.Result-ne'READY'){throw 'Poison client not ready'}
    $brokerLine=$client.Process.StandardOutput.ReadLineAsync();if(-not $brokerLine.Wait(1000)-or $brokerLine.Result-notmatch'^BROKER ([0-9]+)$'){throw 'Missing broker identity'}
    $broker=[Diagnostics.Process]::GetProcessById([int]$matches[1])
    $client.Process.StandardInput.WriteLine('continue')
    $locked=$fixture.Process.StandardOutput.ReadLineAsync();if(-not $locked.Wait(5000)-or $locked.Result-ne'LOCKED'){throw 'Allocator lock not acquired'}
    $fixture.Process.Kill($true);$fixture.Process.WaitForExit();Finish $client
    if(-not $broker.WaitForExit(3000)){throw 'Poisoned broker was not reaped'};$broker.Dispose()
    $server=Server $name;Finish (Managed 'run' $name);Stop-Server $server
    # The private job object must reap a broker if its managed owner is killed.
    $name='owner-'+[guid]::NewGuid().ToString('N');$server=Server $name;$client=Managed 'peer' $name
    $ready=$client.Process.StandardOutput.ReadLineAsync();if(-not $ready.Wait(8000)-or $ready.Result-ne'READY'){throw 'Owner client not ready'}
    $brokerLine=$client.Process.StandardOutput.ReadLineAsync();if(-not $brokerLine.Wait(1000)-or $brokerLine.Result-notmatch'^BROKER ([0-9]+)$'){throw 'Missing owned broker identity'}
    $broker=[Diagnostics.Process]::GetProcessById([int]$matches[1]);$client.Process.Kill();$client.Process.WaitForExit()
    if(-not $broker.WaitForExit(5000)){throw 'Broker outlived terminated owner'};$broker.Dispose();Stop-Server $server
    Finish (Managed 'run' ('absent-'+[guid]::NewGuid().ToString('N'))) $false
    Write-Output 'PASS managed/native paired-process tests'
}finally{foreach($child in $children){if(-not $child.Process.HasExited){$child.Process.Kill($true);$child.Process.WaitForExit()};$child.Process.Dispose()}}
