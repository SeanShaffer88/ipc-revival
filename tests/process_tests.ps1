param([Parameter(Mandatory=$true)][string]$Executable,[string]$Results='')
$ErrorActionPreference='Stop'
$Executable=[IO.Path]::GetFullPath($Executable)
$children=[Collections.Generic.List[object]]::new()
$measurements=[Collections.Generic.List[object]]::new()
$checks=[Collections.Generic.List[string]]::new()
function Start-Worker([string[]]$Arguments){
    $info=[Diagnostics.ProcessStartInfo]::new($Executable)
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardInput=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    foreach($a in $Arguments){$info.ArgumentList.Add($a)}
    $p=[Diagnostics.Process]::Start($info)
    $child=@{Process=$p;Errors=$p.StandardError.ReadToEndAsync()};$children.Add($child)
    return $child
}
function Wait-Line($child,[string]$Expected){
    $task=$child.Process.StandardOutput.ReadLineAsync()
    if(-not $task.Wait(8000) -or $task.Result -ne $Expected){throw "Missing $Expected from worker"}
}
function Finish($child,[bool]$Success=$true){
    if(-not $child.Process.WaitForExit(10000)){ $child.Process.Kill($true);throw 'Worker hung beyond watchdog' }
    $output=$child.Process.StandardOutput.ReadToEnd()
    $errors=$child.Errors.GetAwaiter().GetResult()
    if(($child.Process.ExitCode -eq 0) -ne $Success){throw "Unexpected worker exit $($child.Process.ExitCode): $errors $output"}
    return $output
}
function Stop-Server($server){$server.Process.StandardInput.WriteLine('quit');[void](Finish $server)}
function New-Name(){return 'test-'+[guid]::NewGuid().ToString('N')}
try{
    $name=New-Name
    $server=Start-Worker @('shm-server',$name);Wait-Line $server 'READY'
    foreach($size in @(0,1,1024,65536,262144)){
        $client=Start-Worker @('shm-client',$name,"$size",'100')
        $measurements.Add(((Finish $client)|ConvertFrom-Json))
        $checks.Add("shared-memory roundtrip $size bytes x100")
    }
    # A killed producer must not prevent another independent connection.
    $producer=Start-Worker @('shm-client',$name,'65536','100000')
    Start-Sleep -Milliseconds 200
    if($producer.Process.HasExited){throw 'Producer unexpectedly exited before termination test'}
    $producer.Process.Kill($true);$producer.Process.WaitForExit()
    [void](Finish (Start-Worker @('shm-client',$name,'4096','10')))
    $checks.Add('producer termination followed by independent client')
    Stop-Server $server
    $server=Start-Worker @('shm-server',$name);Wait-Line $server 'READY'
    [void](Finish (Start-Worker @('shm-client',$name,'4096','10')));Stop-Server $server
    $checks.Add('graceful restart and endpoint reuse')
    [void](Finish (Start-Worker @('shm-client',(New-Name),'1','1','200')) $false)
    [void](Finish (Start-Worker @('shm-client',(New-Name),'262145','1')) $false)
    [void](Finish (Start-Worker @('shm-client',(New-Name),'1','0')) $false)
    $checks.Add('absent server and invalid bounds fail')
    $name=New-Name;$server=Start-Worker @('shm-server',$name,'3000');Wait-Line $server 'READY'
    $client=Start-Worker @('shm-client',$name,'4096','1','5000');Wait-Line $server 'REQUEST'
    $server.Process.Kill($true);$server.Process.WaitForExit();[void](Finish $client $false)
    $checks.Add('server termination during request fails without hanging')
    $server=Start-Worker @('shm-server',$name);Wait-Line $server 'READY'
    [void](Finish (Start-Worker @('shm-client',$name,'4096','10')));Stop-Server $server
    $checks.Add('endpoint reuse after server termination')
    $name=New-Name;$server=Start-Worker @('shm-server',$name,'1500');Wait-Line $server 'READY'
    [void](Finish (Start-Worker @('shm-client',$name,'4096','1','150')) $false);Stop-Server $server
    $checks.Add('application response timeout')
    foreach($size in @(0,1,1024,65536,262144)){
        $name=New-Name;$server=Start-Worker @('pipe-server',$name,'100');Wait-Line $server 'READY'
        $measurements.Add(((Finish (Start-Worker @('pipe-client',$name,"$size",'100')))|ConvertFrom-Json))
        [void](Finish $server);$checks.Add("named-pipe baseline $size bytes x100")
    }
    $report=[ordered]@{checks=$checks;measurements=$measurements;note='Single sequential client, 100 round trips per payload, one reused immutable blob. Setup/connection/allocation excluded. Both workers hash every payload; pipe copies payload each request and uses an extra drain acknowledgement. Descriptive comparison, not equivalent protocol overhead or broad performance claim.'}
    if($Results){$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $Results -Encoding UTF8}
    Write-Output ("PASS process integration: "+$checks.Count+" checks")
    $measurements|Format-Table
}finally{
    foreach($child in $children){if(-not $child.Process.HasExited){$child.Process.Kill($true);$child.Process.WaitForExit()};$child.Process.Dispose()}
}
