[CmdletBinding()]
param(
 [ValidateRange(5,3600)][int]$Seconds=60,
 [ValidateRange(.1,10)][double]$Interval=1,
 [ValidateSet('Idle','Interaction','Fullscreen','PostInteraction')][string]$Scenario='Idle',
 [string]$OutputPath='',
 [string]$ReadyPath='',
 [string]$StartPath='',
 [ValidateRange(0,2147483647)][int]$RootProcessId=0,
 [switch]$SelfTest
)
$ErrorActionPreference='Stop'
function Get-NormalizedCpu {
 param([double]$CpuSeconds,[double]$ElapsedSeconds,[int]$LogicalProcessors)
 if($ElapsedSeconds -le 0 -or $LogicalProcessors -le 0){return 0.0}
 return [Math]::Max(0.0,100.0*$CpuSeconds/$ElapsedSeconds/$LogicalProcessors)
}
function Get-IntervalCpuSeconds {
 param([hashtable]$Previous,[hashtable]$Current)
 $delta=0.0
 foreach($key in $Current.Keys){
  if($Previous.ContainsKey($key)){$delta+=[Math]::Max(0.0,$Current[$key].CpuSeconds-$Previous[$key].CpuSeconds)}
  elseif($Current[$key].BirthWithinInterval){$delta+=[Math]::Max(0.0,$Current[$key].CpuSeconds)}
 }
 return $delta
}
function Test-OwnedHelper {
 param([int]$ParentId,[datetime]$CreatedUtc,[string]$Name,[int]$OwnerId,[datetime]$OwnerStartUtc,[bool]$OwnerAlive)
 return $OwnerAlive -and $ParentId -eq $OwnerId -and $CreatedUtc -ge $OwnerStartUtc -and $Name -in @('AetherProvider.exe','AetherWatchdog.exe','AetherWallpaperProvider.exe')
}
function Write-HandshakeFile([string]$path,$data){
 $temporary=$path+'.tmp-'+[guid]::NewGuid().ToString('N')
 try{[IO.File]::WriteAllText($temporary,($data|ConvertTo-Json -Depth 4 -Compress),(New-Object Text.UTF8Encoding $false));[IO.File]::Move($temporary,$path)}finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
if($SelfTest){
 if([Math]::Abs((Get-NormalizedCpu 4 2 8)-25.0) -gt .0001){throw 'CPU normalization failed.'}
 if((Get-NormalizedCpu -1 1 8) -ne 0){throw 'Negative CPU guard failed.'}
 if((Get-NormalizedCpu 1 0 8) -ne 0){throw 'Zero elapsed guard failed.'}
 if((Get-NormalizedCpu 1 2 0) -ne 0){throw 'Zero processor guard failed.'}
 $previous=@{'42:100'=[pscustomobject]@{CpuSeconds=7.0}}
 $exited=@{'42:100'=[pscustomobject]@{CpuSeconds=7.75;BirthWithinInterval=$false;Exited=$true}}
 if([Math]::Abs((Get-IntervalCpuSeconds $previous $exited)-.75) -gt .0001){throw 'Final CPU delta of exited tracked process was omitted.'}
 $reused=@{'42:200'=[pscustomobject]@{CpuSeconds=.25;BirthWithinInterval=$true;Exited=$false}}
 if([Math]::Abs((Get-IntervalCpuSeconds $previous $reused)-.25) -gt .0001){throw 'Recycled PID was subtracted from another process generation.'}
 $late=@{'42:300'=[pscustomobject]@{CpuSeconds=8.0;BirthWithinInterval=$false;Exited=$false}}
 if((Get-IntervalCpuSeconds @{} $late) -ne 0){throw 'Late first sighting was incorrectly charged to the current interval.'}
 $reset=@{'42:100'=[pscustomobject]@{CpuSeconds=6.0;BirthWithinInterval=$false;Exited=$false}}
 if((Get-IntervalCpuSeconds $previous $reset) -ne 0){throw 'Negative process CPU delta was admitted.'}
 $start=[datetime]'2026-10-08T20:00:00Z'
 if(-not (Test-OwnedHelper 42 $start.AddSeconds(1) 'AetherProvider.exe' 42 $start $true)){throw 'Direct provider child was excluded.'}
 if(-not (Test-OwnedHelper 42 $start.AddSeconds(1) 'AetherWatchdog.exe' 42 $start $true)){throw 'Direct watchdog child was excluded.'}
 if(-not (Test-OwnedHelper 42 $start.AddSeconds(1) 'AetherWallpaperProvider.exe' 42 $start $true)){throw 'Owned wallpaper provider omitted from measurement scope.'}
 if(Test-OwnedHelper 77 $start.AddSeconds(1) 'AetherProvider.exe' 42 $start $true){throw 'Another root provider was included.'}
 if(Test-OwnedHelper 42 $start.AddSeconds(-1) 'AetherProvider.exe' 42 $start $true){throw 'Child from a recycled parent PID was included.'}
 if(Test-OwnedHelper 42 $start.AddSeconds(1) 'powershell.exe' 42 $start $true){throw 'Unrelated helper was included.'}
 if(Test-OwnedHelper 42 $start.AddSeconds(1) 'AetherProvider.exe' 42 $start $false){throw 'New helpers were admitted after root identity exited.'}
 Write-Host 'Performance-math self-tests passed (15 checks: normalization, final exit CPU, PID generation and direct helper scope).'
 exit 0
}
if(-not ('AetherMeasureNative' -as [type])){
 Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class AetherMeasureNative {
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr process,out long created,out long exited,out long kernel,out long user);
 [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
 public static bool Read(IntPtr process,out double cpu,out bool alive){
  long created,exited,kernel,user;cpu=0;alive=false;
  if(!GetProcessTimes(process,out created,out exited,out kernel,out user))return false;
  cpu=(kernel+user)/10000000.0;alive=WaitForSingleObject(process,0)==258;return true;
 }
}
"@
}
if(-not $RootProcessId){
 $roots=@(Get-Process -Name AetherUI -ErrorAction SilentlyContinue)
 if($roots.Count -ne 1){foreach($candidate in $roots){$candidate.Dispose()};throw 'Supply -RootProcessId when there is not exactly one AetherUI process.'}
 $RootProcessId=$roots[0].Id;$roots[0].Dispose()
}
$rootProcess=Get-Process -Id $RootProcessId -ErrorAction Stop
if($rootProcess.ProcessName -ne 'AetherUI'){$rootProcess.Dispose();throw 'RootProcessId must identify AetherUI.exe.'}
$rootStartUtc=$rootProcess.StartTime.ToUniversalTime();$rootHandle=$rootProcess.Handle
$tracked=@{};$history=@{};$discoveryFailures=New-Object 'System.Collections.Generic.List[string]'
$rootIdentity='{0}:{1}' -f $RootProcessId,$rootStartUtc.Ticks
$tracked[$rootIdentity]=[pscustomobject]@{Process=$rootProcess;Handle=$rootHandle;ProcessId=$RootProcessId;StartUtc=$rootStartUtc;Name='AetherUI';ParentProcessId=0}
function Add-OwnedProcess($process,[int]$parentId){
 try{
  $created=$process.StartTime.ToUniversalTime();$identity='{0}:{1}' -f $process.Id,$created.Ticks
  if($tracked.ContainsKey($identity)){$process.Dispose();return}
  $tracked[$identity]=[pscustomobject]@{Process=$process;Handle=$process.Handle;ProcessId=$process.Id;StartUtc=$created;Name=$process.ProcessName;ParentProcessId=$parentId}
 }catch{$process.Dispose()}
}
function Get-AetherSnapshot([datetime]$intervalStartUtc){
 $records=@{};$rootCpu=0.0;$rootAlive=$false
 $rootReadable=[AetherMeasureNative]::Read($rootHandle,[ref]$rootCpu,[ref]$rootAlive)
 if($rootReadable -and $rootAlive){
  try{
   $children=@(Get-CimInstance Win32_Process -Filter "ParentProcessId=$RootProcessId" -ErrorAction Stop)
   foreach($child in $children){
    if(-not $child.CreationDate){continue};$created=([datetime]$child.CreationDate).ToUniversalTime()
    if(Test-OwnedHelper ([int]$child.ParentProcessId) $created $child.Name $RootProcessId $rootStartUtc $rootAlive){
     $candidate=Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue
     if($candidate){
      # Re-check live generation and name after CIM discovery; its PID may already have exited/recycled.
      try{if($candidate.StartTime.ToUniversalTime() -ge $rootStartUtc -and [Math]::Abs(($candidate.StartTime.ToUniversalTime()-$created).Ticks) -le 10 -and ($candidate.ProcessName+'.exe') -ieq $child.Name){Add-OwnedProcess $candidate $RootProcessId}else{$candidate.Dispose()}}catch{$candidate.Dispose()}
     }
    }
   }
  }catch{$discoveryFailures.Add($_.Exception.Message)}
 }
 foreach($identity in @($tracked.Keys)){
  $owned=$tracked[$identity];$cpu=0.0;$alive=$false
  if(-not [AetherMeasureNative]::Read($owned.Handle,[ref]$cpu,[ref]$alive)){continue}
  $private=0L;$working=0L
  if($alive){try{$owned.Process.Refresh();$private=$owned.Process.PrivateMemorySize64;$working=$owned.Process.WorkingSet64}catch{}}
  $records[$identity]=[pscustomobject]@{
   Identity=$identity;ProcessId=$owned.ProcessId;StartUtc=$owned.StartUtc.ToString('o');ParentProcessId=$owned.ParentProcessId;Name=$owned.Name
   CpuSeconds=$cpu;PrivateBytes=$private;WorkingSetBytes=$working;Exited=(-not $alive);BirthWithinInterval=($owned.StartUtc -ge $intervalStartUtc)
  }
  if(-not $history.ContainsKey($identity)){$history[$identity]=[pscustomobject]@{Identity=$identity;ProcessId=$owned.ProcessId;StartUtc=$owned.StartUtc.ToString('o');ParentProcessId=$owned.ParentProcessId;Name=$owned.Name;FinalObservedCpuSeconds=0.0;ExitObserved=$false}}
  $history[$identity].FinalObservedCpuSeconds=$cpu;$history[$identity].ExitObserved=(-not $alive)
  if(-not $alive){$tracked.Remove($identity);if($identity -ne $rootIdentity){$owned.Process.Dispose()}}
 }
 return $records
}
if(-not $OutputPath){$sourceRoot=Split-Path -Parent $PSScriptRoot;$OutputPath=Join-Path $sourceRoot ('aether-{0}-{1}.json' -f $Scenario.ToLowerInvariant(),(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if([bool]$ReadyPath -xor [bool]$StartPath){throw 'Supply both fresh -ReadyPath and -StartPath handshake files, or neither.'}
if($ReadyPath){
 $outputDirectory=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath))
 foreach($handshakePath in @($ReadyPath,$StartPath)){
  if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($handshakePath)) -ine $outputDirectory -or (Test-Path -LiteralPath $handshakePath)){throw 'Handshake files must be fresh files in the measurement output directory.'}
 }
 if([IO.Path]::GetFullPath($ReadyPath) -ieq [IO.Path]::GetFullPath($StartPath)){throw 'Ready and start handshake files must have distinct paths.'}
}
$processors=[Environment]::ProcessorCount;$rows=New-Object 'System.Collections.Generic.List[object]'
$lateSightings=0;$finalExits=0;$totalCpu=0.0;$totalElapsed=0.0
try{
 $previous=Get-AetherSnapshot ([datetime]::UtcNow)
 $baselineUtc=[datetime]::UtcNow;$readyUtc=$null;$signalUtc=$null
 if($ReadyPath){
  $readyUtc=[datetime]::UtcNow
  Write-HandshakeFile $ReadyPath ([pscustomobject]@{WorkerProcessId=$PID;RootProcessId=$RootProcessId;RootIdentity=$rootIdentity;Scenario=$Scenario;BaselineUtc=$baselineUtc.ToString('o');ReadyUtc=$readyUtc.ToString('o')})
  $deadline=[datetime]::UtcNow.AddSeconds(15)
  while(-not (Test-Path -LiteralPath $StartPath -PathType Leaf)){
   if([datetime]::UtcNow -ge $deadline){throw 'Measurement start handshake did not arrive within 15 seconds.'}
   Start-Sleep -Milliseconds 25
  }
  if((Get-Item -LiteralPath $StartPath).Length -gt 4096){throw 'Measurement start handshake is oversized.'}
  $signal=Get-Content -LiteralPath $StartPath -Raw|ConvertFrom-Json
  if($signal.WorkerProcessId -ne $PID -or $signal.RootIdentity -ne $rootIdentity -or $signal.Scenario -ne $Scenario){throw 'Measurement start handshake does not match this worker/root/scenario.'}
  $signalUtc=([datetime]$signal.StartUtc).ToUniversalTime()
 }
 $measurementStartUtc=[datetime]::UtcNow;$baselineLeadSeconds=[Math]::Max(0.0,($measurementStartUtc-$baselineUtc).TotalSeconds)
 $lastUtc=$baselineUtc;$clock=[Diagnostics.Stopwatch]::StartNew();$lastTime=-$baselineLeadSeconds
 Write-Host "Sampling $Scenario for $Seconds seconds, root $rootIdentity. Scope: owned AetherUI + direct AetherProvider/AetherWatchdog/AetherWallpaperProvider children."
 while($clock.Elapsed.TotalSeconds -lt $Seconds){
  Start-Sleep -Milliseconds ([int]($Interval*1000))
  $current=Get-AetherSnapshot $lastUtc;$sampleUtc=[datetime]::UtcNow;$now=$clock.Elapsed.TotalSeconds;$elapsed=$now-$lastTime
  $cpu=Get-IntervalCpuSeconds $previous $current;$totalCpu+=$cpu;$totalElapsed+=$elapsed
  $live=@($current.Values|Where-Object {-not $_.Exited});$private=0.0;$working=0.0
  foreach($entry in $live){$private+=$entry.PrivateBytes;$working+=$entry.WorkingSetBytes}
  foreach($identity in $current.Keys){if(-not $previous.ContainsKey($identity) -and -not $current[$identity].BirthWithinInterval){++$lateSightings};if($current[$identity].Exited){++$finalExits}}
  $rows.Add([pscustomobject]@{
   ElapsedSeconds=[Math]::Round($now,6);SampleElapsedSeconds=[Math]::Round($elapsed,6);SampleUtc=$sampleUtc.ToString('o');CpuDeltaSeconds=$cpu
   CpuPercentOfMachine=[Math]::Round((Get-NormalizedCpu $cpu $elapsed $processors),6);CpuPercentOfOneCore=[Math]::Round((Get-NormalizedCpu $cpu $elapsed 1),6)
   PrivateBytes=[long]$private;WorkingSetBytes=[long]$working;PrivateMiB=[Math]::Round($private/1MB,6);WorkingSetMiB=[Math]::Round($working/1MB,6)
   WallpaperProviderCount=@($live|Where-Object {$_.Name -eq 'AetherWallpaperProvider'}).Count;ProcessCount=$live.Count;ProviderCount=@($live|Where-Object {$_.Name -eq 'AetherProvider'}).Count;WatchdogCount=@($live|Where-Object {$_.Name -eq 'AetherWatchdog'}).Count
   Processes=@($current.Values|Sort-Object Identity)
  })
  $previous=$current;$lastTime=$now;$lastUtc=$sampleUtc
  if($live.Count -eq 0){break}
 }
 $clock.Stop()
 $cpuStats=$rows|Measure-Object CpuPercentOfMachine -Maximum;$memoryStats=$rows|Measure-Object PrivateMiB -Average -Maximum
 $workingStats=$rows|Measure-Object WorkingSetMiB -Average -Maximum;$processStats=$rows|Measure-Object ProcessCount -Maximum
 $liveRows=@($rows.ToArray()|Where-Object {$_.ProcessCount -gt 0});$growth=$null;$slope=$null
 if($liveRows.Count -gt 1){
  $growth=$liveRows[-1].PrivateMiB-$liveRows[0].PrivateMiB
  $n=$liveRows.Count;$sumX=0.0;$sumY=0.0;$sumXY=0.0;$sumXX=0.0
  foreach($sample in $liveRows){$x=$sample.ElapsedSeconds;$y=$sample.PrivateMiB;$sumX+=$x;$sumY+=$y;$sumXY+=$x*$y;$sumXX+=$x*$x}
  $denominator=$n*$sumXX-$sumX*$sumX;if($denominator -gt 0){$slope=60*($n*$sumXY-$sumX*$sumY)/$denominator}
 }
 $result=[pscustomobject]@{
  BaselineUtc=$baselineUtc.ToString('o');ReadyUtc=$(if($readyUtc){$readyUtc.ToString('o')}else{$null});StartSignalUtc=$(if($signalUtc){$signalUtc.ToString('o')}else{$null});MeasurementStartUtc=$measurementStartUtc.ToString('o');MeasurementEndUtc=[datetime]::UtcNow.ToString('o')
  CapturedUtc=[datetime]::UtcNow.ToString('o');Scenario=$Scenario;RequestedSeconds=$Seconds;ActualSeconds=[Math]::Round($clock.Elapsed.TotalSeconds,6)
  RequestedIntervalSeconds=$Interval;LogicalProcessors=$processors;OsVersion=[Environment]::OSVersion.VersionString;RootProcessId=$RootProcessId;RootStartUtc=$rootStartUtc.ToString('o');RootIdentity=$rootIdentity
  Scope='One AetherUI PID/start-time identity plus only direct named AetherProvider/AetherWatchdog/AetherWallpaperProvider children born after that root. Retained handles collect final CPU for observed exits. No arbitrary descendants, fixture, probe, DWM, GPU, temperature or game frame times.'
  SamplingLimits='First CPU interval begins at initial baseline; ready/start lead is included in its denominator and reported separately. Snapshot/discovery work extends the requested interval. Helpers born and exited entirely between snapshots are unobserved and their CPU is undercounted. First sightings older than the prior snapshot have no defensible delta and are omitted. Exit CPU is recovered only for already observed retained process handles. Memory is instantaneous live-process total; CPU percentages can exceed 100 percent of one core.'
  InitialBaselineLeadSeconds=$baselineLeadSeconds;CpuIntervalSeconds=$totalElapsed;TotalObservedCpuDeltaSeconds=$totalCpu;LateFirstSightings=$lateSightings;FinalObservedExits=$finalExits;DiscoveryFailures=$discoveryFailures.ToArray()
  AverageCpuPercentOfMachine=(Get-NormalizedCpu $totalCpu $totalElapsed $processors);AverageCpuPercentOfOneCore=(Get-NormalizedCpu $totalCpu $totalElapsed 1)
  PeakSampleCpuPercentOfMachine=$cpuStats.Maximum;AveragePrivateMiB=$memoryStats.Average;PeakPrivateMiB=$memoryStats.Maximum
  AverageWorkingSetMiB=$workingStats.Average;PeakWorkingSetMiB=$workingStats.Maximum;PeakProcessCount=$processStats.Maximum
  FirstLivePrivateMiB=$(if($liveRows.Count){$liveRows[0].PrivateMiB}else{$null});LastLivePrivateMiB=$(if($liveRows.Count){$liveRows[-1].PrivateMiB}else{$null})
  PrivateGrowthMiB=$growth;PrivateSlopeMiBPerMinute=$slope;ObservedProcesses=@($history.Values|Sort-Object Identity);Samples=$rows.ToArray()
 }
 if(-not $OutputPath){$sourceRoot=Split-Path -Parent $PSScriptRoot;$OutputPath=Join-Path $sourceRoot ('aether-{0}-{1}.json' -f $Scenario.ToLowerInvariant(),(Get-Date -Format 'yyyyMMdd-HHmmss'))}
 $result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $OutputPath -Encoding utf8
 $result|Select-Object Scenario,ActualSeconds,AverageCpuPercentOfMachine,AverageCpuPercentOfOneCore,PeakSampleCpuPercentOfMachine,AveragePrivateMiB,PeakPrivateMiB,PrivateGrowthMiB,PrivateSlopeMiBPerMinute|Format-List
 Write-Host "Saved: $OutputPath"
}finally{
 foreach($owned in $tracked.Values){if($owned.Process -ne $rootProcess){$owned.Process.Dispose()}}
 $rootProcess.Dispose()
}
exit 0



