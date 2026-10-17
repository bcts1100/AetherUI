[CmdletBinding()]
param([ValidateRange(5,3600)][int]$Seconds=60,[ValidateRange(1,10)][int]$Interval=1,[int]$RootProcessId=0,[string]$OutputPath='',[switch]$IncludeDwm,[switch]$SelfTest)
$ErrorActionPreference='Stop'
function Get-EngineToken([string]$instance){
 if($instance -notmatch '^pid_(\d+)_(luid_.+_phys_\d+_eng_\d+)_engtype_(.+)$'){return $null}
 return [pscustomobject]@{ProcessId=[int]$Matches[1];Engine=$Matches[2];Type=$Matches[3]}
}
function Get-GpuProcessIdentity($process){
 try{if($null -eq $process.StartTime){return $null};return '{0}:{1}' -f $process.Id,$process.StartTime.ToUniversalTime().Ticks}catch{return $null}
}
function Convert-GpuIdentities([hashtable]$scope){
 return @($scope.Keys|Sort-Object|ForEach-Object {[pscustomobject]@{ProcessId=[int]$_;Identity=[string]$scope[$_]}})
}
function Get-ScopedEngines([object[]]$samples,[hashtable]$before,[hashtable]$after){
 $engines=@{};$rejected=0;$matched=0
 foreach($sample in $samples){
  $token=Get-EngineToken $sample.InstanceName
  if(-not $token -or -not $before.ContainsKey($token.ProcessId) -or -not $after.ContainsKey($token.ProcessId)){continue}
  if($before[$token.ProcessId] -ne $after[$token.ProcessId]){++$rejected;continue}
  $value=[double]$sample.CookedValue
  if($sample.Status -notin @(0,1) -or [double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0){++$rejected;continue}
  ++$matched;$key=$token.Engine+'_'+$token.Type
  if(-not $engines.ContainsKey($key)){$engines[$key]=[pscustomobject]@{Engine=$token.Engine;Type=$token.Type;Percent=0.0}}
  $engines[$key].Percent+=$value
 }
 $peak=$null;if($matched -gt 0){$peak=($engines.Values|Measure-Object Percent -Maximum).Maximum}
 return [pscustomobject]@{Matched=$matched;Rejected=$rejected;BusiestEnginePercent=$peak;Engines=@($engines.Values|Sort-Object Engine,Type)}
}
if($SelfTest){
 $scope=@{42='42:1';84='84:2'}
 $samples=@(
  [pscustomobject]@{InstanceName='pid_42_luid_0x1_0x2_phys_0_eng_0_engtype_3D';Status=0;CookedValue=3.0},
  [pscustomobject]@{InstanceName='pid_84_luid_0x1_0x2_phys_0_eng_0_engtype_3D';Status=1;CookedValue=4.0},
  [pscustomobject]@{InstanceName='pid_42_luid_0x1_0x2_phys_0_eng_1_engtype_Copy';Status=0;CookedValue=2.0},
  [pscustomobject]@{InstanceName='pid_99_luid_0x1_0x2_phys_0_eng_0_engtype_3D';Status=0;CookedValue=90.0}
 )
 $result=Get-ScopedEngines $samples $scope $scope
 if($result.Matched -ne 3 -or $result.BusiestEnginePercent -ne 7 -or $result.Engines.Count -ne 2){throw 'Scoped GPU engines incorrectly combined or included unrelated PID.'}
 $recycled=Get-ScopedEngines $samples $scope @{42='42:9';84='84:2'}
 if($recycled.Matched -ne 1 -or $recycled.Rejected -ne 2 -or $recycled.BusiestEnginePercent -ne 4){throw 'GPU PID generation check failed.'}
 $invalid=Get-ScopedEngines @([pscustomobject]@{InstanceName=$samples[0].InstanceName;Status=5;CookedValue=3}) $scope $scope
 if($invalid.Matched -ne 0 -or $null -ne $invalid.BusiestEnginePercent){throw 'Invalid GPU counter was reported as zero.'}
 $zero=Get-ScopedEngines @([pscustomobject]@{InstanceName=$samples[0].InstanceName;Status=0;CookedValue=0}) $scope $scope
 if($zero.BusiestEnginePercent -ne 0 -or $zero.Matched -ne 1){throw 'A valid zero GPU sample was lost.'}
 if($null -ne (Get-EngineToken 'unknown')){throw 'Malformed GPU instance accepted.'}
 if($null -ne (Get-GpuProcessIdentity ([pscustomobject]@{Id=1;StartTime=$null}))){throw 'Protected process with unavailable creation time was admitted.'}
 $identity=Get-GpuProcessIdentity ([pscustomobject]@{Id=42;StartTime=[datetime]'2026-10-09T00:00:00Z'})
 if(-not $identity.StartsWith('42:')){throw 'GPU process identity omitted its live generation.'}
 $serialized=[pscustomobject]@{BeforeIdentities=@(Convert-GpuIdentities $scope);AfterIdentities=@(Convert-GpuIdentities $scope)}|ConvertTo-Json -Depth 5
 $roundtrip=$serialized|ConvertFrom-Json
 if($roundtrip.BeforeIdentities.Count -ne 2 -or $roundtrip.BeforeIdentities[0].ProcessId -ne 42 -or $roundtrip.AfterIdentities[1].Identity -ne '84:2'){throw 'GPU identities did not survive Windows PowerShell JSON serialization.'}
 foreach($smallScope in @(@{},@{42='42:1'})){
  $smallJson=[pscustomobject]@{Identities=@(Convert-GpuIdentities $smallScope)}|ConvertTo-Json -Depth 5
  $smallResult=$smallJson|ConvertFrom-Json
  if($smallResult.Identities -isnot [array] -or $smallResult.Identities.Count -ne $smallScope.Count){throw 'GPU identity JSON cardinality must preserve zero/one records as arrays.'}
 }
 Write-Host 'GPU scope self-tests passed: engine aggregation, unrelated PID, PID generation, invalid/zero and malformed instances.';exit 0
}
if(-not $RootProcessId){
 $roots=@(Get-Process -Name AetherUI -ErrorAction SilentlyContinue)
 if($roots.Count -ne 1){foreach($process in $roots){$process.Dispose()};throw 'Supply the exact AetherUI root PID.'}
 $RootProcessId=$roots[0].Id;foreach($process in $roots){$process.Dispose()}
}
$root=Get-Process -Id $RootProcessId
if($root.ProcessName -ne 'AetherUI'){$root.Dispose();throw 'Root must identify AetherUI.exe.'}
$rootStart=$root.StartTime.ToUniversalTime();$rootIdentity='{0}:{1}' -f $RootProcessId,$rootStart.Ticks
function Get-GpuScope{
 $scope=@{}
 try{$root.Refresh();if($root.HasExited -or $root.StartTime.ToUniversalTime() -ne $rootStart){return $scope}}catch{return $scope}
 $scope[$RootProcessId]=$rootIdentity
 foreach($child in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$RootProcessId" -ErrorAction Stop)){
  if($child.Name -notin @('AetherProvider.exe','AetherWatchdog.exe','AetherWallpaperProvider.exe') -or -not $child.CreationDate){continue}
  $birth=([datetime]$child.CreationDate).ToUniversalTime();if($birth -lt $rootStart){continue}
  $live=Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue
  if($live){try{if($live.StartTime -and ($live.ProcessName+'.exe') -ieq $child.Name -and [Math]::Abs(($live.StartTime.ToUniversalTime()-$birth).Ticks) -le 10){$scope[$live.Id]='{0}:{1}' -f $live.Id,$live.StartTime.ToUniversalTime().Ticks}}finally{$live.Dispose()}}
 }
 return $scope
}
$dwmDiscoveryFailures=New-Object 'System.Collections.Generic.List[string]'
function Get-DwmScope{
 $scope=@{}
 foreach($process in @(Get-Process -Name dwm -ErrorAction SilentlyContinue)){
  try{$identity=Get-GpuProcessIdentity $process;if($identity){$scope[$process.Id]=$identity}else{$reason='DWM PID '+$process.Id+' creation time unavailable; excluded to preserve PID-generation checks.';if(-not $dwmDiscoveryFailures.Contains($reason)){$dwmDiscoveryFailures.Add($reason)}}}finally{$process.Dispose()}
 }
 return $scope
}
if(-not $OutputPath){$OutputPath=Join-Path (Split-Path -Parent $PSScriptRoot) ('aether-gpu-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'.json')}
$rows=New-Object 'System.Collections.Generic.List[object]';$errors=New-Object 'System.Collections.Generic.List[string]'
$clock=[Diagnostics.Stopwatch]::StartNew();$started=[datetime]::UtcNow;$available=$true
try{
 while($clock.Elapsed.TotalSeconds -lt $Seconds){
  try{
   $before=Get-GpuScope;$dwmBefore=@{};if($IncludeDwm){$dwmBefore=Get-DwmScope}
   if($before.Count -eq 0){break}
   $sets=@(Get-Counter -Counter '\GPU Engine(*)\Utilization Percentage' -SampleInterval $Interval -MaxSamples 2 -ErrorAction Stop)
   $after=Get-GpuScope;$dwmAfter=@{};if($IncludeDwm){$dwmAfter=Get-DwmScope}
   $last=$sets[-1];$sample=Get-ScopedEngines @($last.CounterSamples) $before $after
   $dwm=$null;if($IncludeDwm){$dwm=Get-ScopedEngines @($last.CounterSamples) $dwmBefore $dwmAfter}
   $rows.Add([pscustomobject]@{ElapsedSeconds=$clock.Elapsed.TotalSeconds;SampleUtc=$last.Timestamp.ToUniversalTime().ToString('o');RootAndHelpers=$sample;DwmSystemObservation=$dwm;BeforeIdentities=@(Convert-GpuIdentities $before);AfterIdentities=@(Convert-GpuIdentities $after)})
  }catch{$available=$false;$errors.Add($_.Exception.Message);break}
 }
}finally{$root.Dispose();$clock.Stop()}
$valid=@($rows|Where-Object {$_.RootAndHelpers.Matched -gt 0});$summary=$null
if($valid.Count -gt 0){$stats=$valid|ForEach-Object {$_.RootAndHelpers.BusiestEnginePercent}|Measure-Object -Average -Maximum;$summary=[pscustomobject]@{ValidSamples=$valid.Count;AverageBusiestEnginePercent=$stats.Average;PeakBusiestEnginePercent=$stats.Maximum}}
$result=[pscustomobject]@{
 StartedUtc=$started.ToString('o');EndedUtc=[datetime]::UtcNow.ToString('o');RootIdentity=$rootIdentity;RequestedSeconds=$Seconds;ActualSeconds=$clock.Elapsed.TotalSeconds
 CounterAvailable=$available;Summary=$summary;Errors=$errors.ToArray();DwmDiscoveryFailures=$dwmDiscoveryFailures.ToArray()
 Scope='AetherUI plus direct named owned helpers, PID generation checked before and after each counter batch. Optional DWM is system compositor observation, never app attribution.'
 Limits='Busiest physical engine is maximum after summing scoped process contexts on each engine, not sum across independent engines. Missing/invalid samples remain unavailable rather than zero. Two-sample counter batches and scope discovery add observer overhead. GPU memory, power, temperature and game frame times are not measured; Windows GPU process memory counters have documented accuracy limitations.'
 Sources=@('https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/','https://learn.microsoft.com/en-us/troubleshoot/windows-client/performance/gpu-process-memory-counters-report-wrong-value')
 Samples=$rows.ToArray()
}
$result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $OutputPath -Encoding utf8
$result|Select-Object RootIdentity,CounterAvailable,ActualSeconds,Summary,Errors|Format-List