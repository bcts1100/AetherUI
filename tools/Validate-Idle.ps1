[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BinaryPath,[Parameter(Mandatory=$true)][string]$ArtifactDirectory,[ValidateRange(5,3600)][int]$Seconds=60,[ValidateRange(0,60)][int]$WarmupSeconds=10,[switch]$Gpu,[switch]$EnableExampleWidgets,[switch]$DisableFullscreenSuppression,[string]$EnvironmentLabel='Input-occluded desktop; interactive validation pending')
$ErrorActionPreference='Stop'
$stage=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')+'\'
$binary=[IO.Path]::GetFullPath($BinaryPath);$run=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $binary.StartsWith($stage,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $binary -PathType Leaf)){throw 'Binary must be an existing staged executable.'}
if(-not $run.StartsWith($stage,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $run)){throw 'Choose a fresh staged artifact directory.'}
if(Get-Process -Name AetherUI -ErrorAction SilentlyContinue){throw 'An existing AetherUI process must be closed before this isolated measurement.'}
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class AetherIdleNative {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 public delegate bool MonitorProc(IntPtr h,IntPtr dc,IntPtr r,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]public struct MonitorInfo{public int Size;public Rect Bounds,Work;public int Flags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Name;}
 [DllImport("user32.dll")]static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorProc proc,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool GetMonitorInfo(IntPtr h,ref MonitorInfo info);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc proc,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,System.Text.StringBuilder text,int count);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("kernel32.dll")]public static extern bool GetExitCodeProcess(IntPtr h,out uint code);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")]static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,System.Text.StringBuilder text,int count);
 public static int PanelCount(int expected,string title){int count=0;bool complete=EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==expected&&IsWindowVisible(h)){var text=new System.Text.StringBuilder(256);GetWindowText(h,text,text.Capacity);if(text.ToString()==title)++count;}return true;},IntPtr.Zero);return complete?count:-1;}
 public static int WidgetCount(int expected){int count=0;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(pid==expected){var text=new System.Text.StringBuilder(256);GetWindowText(h,text,text.Capacity);if(text.ToString().StartsWith("Aether extension: "))++count;}return true;},IntPtr.Zero);return count;}
 public static IntPtr Controller(int expected){IntPtr result=IntPtr.Zero;EnumWindows((h,p)=>{uint pid;GetWindowThreadProcessId(h,out pid);var c=new System.Text.StringBuilder(128);GetClassName(h,c,c.Capacity);if(pid==expected&&c.ToString()=="Aether.Controller.v3"){result=h;return false;}return true;},IntPtr.Zero);return result;}
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public static string[] WorkAreas(){var list=new List<string>();EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,(h,dc,r,p)=>{var i=new MonitorInfo{Size=Marshal.SizeOf(typeof(MonitorInfo))};if(GetMonitorInfo(h,ref i))list.Add(i.Name+":"+i.Work.Left+","+i.Work.Top+","+i.Work.Right+","+i.Work.Bottom);return true;},IntPtr.Zero);list.Sort();return list.ToArray();}
 public static string InputOcclusion(){var h=FindWindow("LockScreenInputOcclusionFrame",null);if(h==IntPtr.Zero||!IsWindowVisible(h))return "";uint pid;GetWindowThreadProcessId(h,out pid);Rect r;GetWindowRect(h,out r);return "LockScreenInputOcclusionFrame PID="+pid+" bounds="+r.Left+","+r.Top+","+r.Right+","+r.Bottom;}
}
"@
function Get-IdlePanelSnapshot([Diagnostics.Process]$Process,[int]$ExpectedCount){
 $aliveBefore=$Process -and -not $Process.HasExited
 $topBars=$null;$docks=$null
 if($aliveBefore){
  $topBars=[AetherIdleNative]::PanelCount($Process.Id,'Aether Top Bar')
  $docks=[AetherIdleNative]::PanelCount($Process.Id,'Aether Dock')
 }
 $aliveAfter=$Process -and -not $Process.HasExited
 $complete=$aliveBefore -and $aliveAfter -and $topBars -ge 0 -and $docks -ge 0
 [pscustomobject]@{ObservedUtc=[datetime]::UtcNow.ToString('o');HeldAppPid=$(if($Process){$Process.Id}else{$null});ProcessAliveBefore=$aliveBefore;ProcessAliveAfter=$aliveAfter;ExpectedPerSurfaceCount=$ExpectedCount;VisibleTopBarCount=$topBars;VisibleDockCount=$docks;ObservationComplete=$complete;MatchesExpected=($complete -and $topBars -eq $ExpectedCount -and $docks -eq $ExpectedCount)}
}
[IO.Directory]::CreateDirectory($run)|Out-Null;$profile=Join-Path $run 'profile';[IO.Directory]::CreateDirectory($profile)|Out-Null
$fullscreenSetting=if($DisableFullscreenSuppression.IsPresent){0}else{1}
$settings=@"
[AetherSettings]
version=3
[behavior]
hideTaskbar=0
hideFullscreen=$fullscreenSetting
windowPreviews=1
[layout]
edge=bottom
iconSize=48
dockGap=10
topBar=1
dock=1
autoHide=0
monitorOnly=1
reserveDock=1
"@
[IO.File]::WriteAllText((Join-Path $profile 'settings-v3.ini'),$settings,(New-Object Text.UTF8Encoding $false))
if($EnableExampleWidgets){
 $extensionDirectory=Join-Path $profile 'extensions';[IO.Directory]::CreateDirectory($extensionDirectory)|Out-Null
 $manifest=@"
[AetherPlugin]
version=1
[plugin]
id=measure-native
name=Measurement native widgets
[widget:clock]
type=clock
label=Clock
x=24
y=24
[widget:focus]
type=focused-app
label=Focused app
x=24
y=164
[widget:studio]
type=quick-action
label=Desktop Studio
action=studio
x=24
y=304
[AetherInstallation]
version=1
enabled=1
order=0
"@
 [IO.File]::WriteAllText((Join-Path $extensionDirectory 'preset-000001.aetherplugin'),$manifest,(New-Object Text.UTF8Encoding $false))
}
$observerPreviousDpi=[AetherIdleNative]::SetThreadDpiAwarenessContext([IntPtr](-4));$observerDpiSet=$observerPreviousDpi -ne [IntPtr]::Zero;
$before=@([AetherIdleNative]::WorkAreas());$occlusionBefore=[AetherIdleNative]::InputOcclusion();$owned=$null;$normal=$false;$restored=$false;$failure=$null;$panelExposureStart=$null;$panelExposureEnd=$null
try{
 $owned=Start-Process -FilePath $binary -ArgumentList @('--settings-dir',('"'+$profile+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'app-output.log') -RedirectStandardError (Join-Path $run 'app-error.log')
 $handle=$owned.Handle;$deadline=[datetime]::UtcNow.AddSeconds(15);$controller=[IntPtr]::Zero
 while($controller -eq [IntPtr]::Zero -and [datetime]::UtcNow -lt $deadline){$controller=[AetherIdleNative]::Controller($owned.Id);Start-Sleep -Milliseconds 50}
 if($controller -eq [IntPtr]::Zero){throw 'Owned controller did not start.'}
 Start-Sleep -Seconds $WarmupSeconds
 $nativeWidgetCount=[AetherIdleNative]::WidgetCount($owned.Id);if($EnableExampleWidgets -and $nativeWidgetCount -ne 3){throw "Expected3owned native widgets, observed $nativeWidgetCount."}
 # Record surface exposure outside the CPU/GPU sampling intervals only.
 $panelExposureStart=Get-IdlePanelSnapshot $owned $before.Count
 & (Join-Path $PSScriptRoot 'Measure.ps1') -RootProcessId $owned.Id -Scenario Idle -Seconds $Seconds -OutputPath (Join-Path $run 'idle.json')
 if($LASTEXITCODE -ne 0){throw 'CPU/private-memory measurement failed.'}
 if($Gpu){& (Join-Path $PSScriptRoot 'Measure-Gpu.ps1') -RootProcessId $owned.Id -Seconds 30 -IncludeDwm -OutputPath (Join-Path $run 'gpu.json')}
 $panelExposureEnd=Get-IdlePanelSnapshot $owned $before.Count
}catch{$failure=$_.Exception.Message}finally{
 $code=$null
 if($owned){
  if(-not $owned.HasExited){$controller=[AetherIdleNative]::Controller($owned.Id);if($controller -ne [IntPtr]::Zero){[AetherIdleNative]::PostMessage($controller,0x10,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}}
  if($owned.WaitForExit(15000)){$exit=[uint32]0;if([AetherIdleNative]::GetExitCodeProcess($owned.Handle,[ref]$exit)){$code=[int]$exit;$normal=$code -eq 0}}else{$failure='Owned app did not exit within15seconds; it was left for diagnosis.'}
  $owned.Dispose()
 }
 $deadline=[datetime]::UtcNow.AddSeconds(6)
 do{$after=@([AetherIdleNative]::WorkAreas());$restored=($before -join '|') -eq ($after -join '|');if($restored){break};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow -lt $deadline)
 $occlusionAfter=[AetherIdleNative]::InputOcclusion();$observerDpiRestored=$false;if($observerDpiSet){$observerDpiRestored=[AetherIdleNative]::SetThreadDpiAwarenessContext($observerPreviousDpi) -ne [IntPtr]::Zero}
 $gpuObservation=$null;if(Test-Path -LiteralPath (Join-Path $run 'gpu.json')){$gpuObservation=Get-Content -LiteralPath (Join-Path $run 'gpu.json') -Raw|ConvertFrom-Json}
 [pscustomobject]@{ObserverPerMonitorDpiContextSet=$observerDpiSet;ObserverDpiContextRestored=$observerDpiRestored;EnabledExampleWidgets=$EnableExampleWidgets.IsPresent;DisableFullscreenSuppression=$DisableFullscreenSuppression.IsPresent;FullscreenSuppression=(-not $DisableFullscreenSuppression.IsPresent);ExpectedVisibleTopBarCount=$before.Count;ExpectedVisibleDockCount=$before.Count;PanelExposureBeforeSampling=$panelExposureStart;PanelExposureAfterSampling=$panelExposureEnd;OwnedNativeWidgetCount=$nativeWidgetCount;GpuRequested=$Gpu.IsPresent;GpuCounterAvailable=$(if($gpuObservation){$gpuObservation.CounterAvailable}else{$null});GpuHasScopedSamples=$(if($gpuObservation){$null -ne $gpuObservation.Summary}else{$null});Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash;Environment=$EnvironmentLabel;InputOcclusionBefore=$occlusionBefore;InputOcclusionAfter=$occlusionAfter;WarmupSeconds=$WarmupSeconds;IdleSeconds=$Seconds;NormalExit=$normal;ExitCode=$code;WorkAreasRestored=$restored;WorkAreasBefore=$before;WorkAreasAfter=$after;Failure=$failure;Success=($normal -and $restored -and -not $failure);Limits='No input or interactive workload. Hidden launch/fresh profile/default top bars+docks; no fixtures. Input occlusion and observer effects make this an environment-specific background sample, not normal desktop/game or steady-state optimization proof. CPU/memory and GPU phases are separate. Owned visible panels are observed only before CPU and after the final sampling phase; matching endpoints do not establish continuous visibility. Fullscreen suppression is profile-controlled and recorded; input/session changes can alter surface exposure.'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'runtime.json') -Encoding utf8
}
if($failure){throw $failure};if(-not $normal -or -not $restored){throw 'Cleanup did not complete normally; inspect runtime.json.'}