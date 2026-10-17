[CmdletBinding()]
param(
 [ValidateSet('Release','Debug')][string]$Configuration='Release',
 [string]$BinaryPath='',
 [string]$ArtifactDirectory='',
 [ValidateRange(5,3600)][int]$IdleSeconds=180,
 [ValidateRange(5,3600)][int]$InteractionSeconds=120,
 [ValidateRange(5,600)][int]$PostInteractionSeconds=30,
 [ValidateRange(0,120)][int]$WarmupSeconds=10,
 [ValidateRange(.1,10)][double]$Interval=1,
 [switch]$RequireDockFlyout,
 [switch]$SettledVisualCapture,
 [switch]$SelfTest,
 [switch]$FixtureMode,
 [string]$FixtureStatePath='',
 [ValidateRange(2,24)][int]$FixtureWindows=16,
 [int]$FixtureX=40,[int]$FixtureY=100
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSEdition -eq 'Core'){throw 'Run this native WinForms harness with C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe (Windows PowerShell 5.1).'}
function Assert-DockGroupCards([object[]]$cards,[int]$expected,[int]$ownedProcessId,[int]$fixtureProcessId){
 if(@($cards).Count -ne $expected){throw "Regular dock benchmark expected $expected visible cards, observed $(@($cards).Count). This scenario requires the regular six-card page; compact pages are unsupported."}
 $distinct=New-Object 'System.Collections.Generic.HashSet[long]'
 foreach($card in $cards){
  if($card.ProcessId -ne $ownedProcessId -or @($card.ExpectedSources).Count -ne 1 -or -not $card.ExpectedSources[0].Alive -or $card.ExpectedSources[0].ProcessId -ne $fixtureProcessId){throw 'Visible dock card does not map uniquely to a live owned fixture HWND/PID.'}
  $source=[long]$card.ExpectedSources[0].Handle
  if($source -le 0 -or -not $distinct.Add($source)){throw 'Visible dock cards contain a missing or duplicated fixture HWND.'}
 }
 if($distinct.Count -ne $expected){throw 'Visible dock card count does not match distinct owned fixture HWNDs.'}
}
function Get-DockGeometryKey($target){
 return '{0}:{1}:{2}:{3}:{4}:{5}:{6}:{7}:{8}:{9}:{10}' -f $target.Handle,$target.Bounds.Left,$target.Bounds.Top,$target.Bounds.Right,$target.Bounds.Bottom,$target.Client.Left,$target.Client.Top,$target.Client.Right,$target.Client.Bottom,$target.Scale,$target.Monitor.Name
}
function Get-DockHoverTarget($geometry){
 $scale=[double]$geometry.Scale
 if([double]::IsNaN($scale) -or [double]::IsInfinity($scale) -or $scale -le 0 -or $geometry.Client.Right -le 0 -or $geometry.Client.Bottom -le 0 -or $geometry.Bounds.Right -le $geometry.Bounds.Left -or $geometry.Bounds.Bottom -le $geometry.Bounds.Top){throw 'Requested native dock geometry is not drawable.'}
 $localX=[int](96*$scale);$localY=[int]($geometry.Client.Bottom/2)
 if($localX -ge $geometry.Client.Right -or $localY -lt 0 -or $localY -ge $geometry.Client.Bottom){throw 'Requested dock geometry cannot contain the established first pinned fixture target.'}
 return [pscustomobject]@{Handle=$geometry.Handle;Bounds=$geometry.Bounds;Client=$geometry.Client;Scale=$scale;Monitor=$geometry.Monitor;LocalX=$localX;LocalY=$localY;ScreenX=$geometry.Bounds.Left+$localX;ScreenY=$geometry.Bounds.Top+$localY}
}
function Assert-DockPointerUnchanged($observed,$requested,$cycle=$null){
 if($observed.CursorX -ne $requested.ScreenX -or $observed.CursorY -ne $requested.ScreenY){
  if($null -ne $cycle){$script:pointerInterference=$true;$cycle.ExternalCursorMovement=[pscustomobject]@{Utc=[datetime]::UtcNow.ToString('o');ExpectedX=$requested.ScreenX;ExpectedY=$requested.ScreenY;ActualX=$observed.CursorX;ActualY=$observed.CursorY}}
  throw 'External cursor movement interrupted the owned dock hover; no relocation was attempted.'
 }
}
function Get-DockHoverRelocation($current,$requested,$observed,$seen){
 Assert-DockPointerUnchanged $observed $requested
 if($current.Handle -ne $requested.Handle -or $current.Monitor.Name -ne $requested.Monitor.Name){throw 'Native dock identity or requested monitor changed during the hover phase.'}
 $key=Get-DockGeometryKey $current
 if($key -eq (Get-DockGeometryKey $requested)){return $null}
 if(-not $seen.Add($key)){throw 'Native dock geometry returned to an already targeted layout; another cursor relocation is unsupported.'}
 return $current
}
if($SelfTest){
 # Pure validation contracts only: no native UI, app, process or hardware run.
 $valid=@(1..6|ForEach-Object {[pscustomobject]@{ProcessId=42;ExpectedSources=@([pscustomobject]@{Handle=[long]$_;ProcessId=84;Alive=$true})}})
 Assert-DockGroupCards $valid 6 42 84
 $missingRejected=$false;try{Assert-DockGroupCards @($valid|Select-Object -First 5) 6 42 84}catch{$missingRejected=$true}
 if(-not $missingRejected){throw 'Missing-card validation self-check failed.'}
 $duplicates=@(1..6|ForEach-Object {[pscustomobject]@{ProcessId=42;ExpectedSources=@([pscustomobject]@{Handle=[long]1;ProcessId=84;Alive=$true})}})
 $duplicateRejected=$false;try{Assert-DockGroupCards $duplicates 6 42 84}catch{$duplicateRejected=$true}
 if(-not $duplicateRejected){throw 'Duplicate-HWND validation self-check failed.'}
 $original=[pscustomobject]@{Handle=12;Bounds=[pscustomobject]@{Left=1408;Top=1257;Right=2032;Bottom=1365};Client=[pscustomobject]@{Left=0;Top=0;Right=624;Bottom=108};Scale=1.5;Monitor=[pscustomobject]@{Name='fixture-monitor'}}
 $changed=[pscustomobject]@{Handle=12;Bounds=[pscustomobject]@{Left=1366;Top=1257;Right=2074;Bottom=1365};Client=[pscustomobject]@{Left=0;Top=0;Right=708;Bottom=108};Scale=1.5;Monitor=[pscustomobject]@{Name='fixture-monitor'}}
 $target=Get-DockHoverTarget $original;$current=Get-DockHoverTarget $changed
 if($target.ScreenX -ne 1552 -or $target.ScreenY -ne 1311 -or $current.ScreenX -ne 1510 -or $current.ScreenY -ne 1311){throw 'Changed native dock geometry did not retain the first pinned fixture target.'}
 $seen=New-Object 'System.Collections.Generic.HashSet[string]';$seen.Add((Get-DockGeometryKey $target))|Out-Null
 $cursor=[pscustomobject]@{CursorX=1552;CursorY=1311}
 if($null -ne (Get-DockHoverRelocation $target $target $cursor $seen)){throw 'Unchanged geometry requested a pointer relocation.'}
 $relocation=Get-DockHoverRelocation $current $target $cursor $seen
 if($null -eq $relocation -or $relocation.ScreenX -ne 1510){throw 'Real changed geometry did not request the new target.'}
 $revisitedRejected=$false;try{Get-DockHoverRelocation $target $current ([pscustomobject]@{CursorX=1510;CursorY=1311}) $seen|Out-Null}catch{$revisitedRejected=$true}
 if(-not $revisitedRejected){throw 'Repeated geometry requested another cursor move.'}
 $movedRejected=$false;try{Get-DockHoverRelocation $current $target ([pscustomobject]@{CursorX=1553;CursorY=1311}) $seen|Out-Null}catch{$movedRejected=$true}
 if(-not $movedRejected){throw 'External pointer motion was accepted for relocation.'}
 $changedMonitor=[pscustomobject]@{Handle=12;Bounds=$changed.Bounds;Client=$changed.Client;Scale=1.5;Monitor=[pscustomobject]@{Name='other-monitor'}}
 $monitorRejected=$false;try{Get-DockHoverRelocation (Get-DockHoverTarget $changedMonitor) $target $cursor $seen|Out-Null}catch{$monitorRejected=$true}
 if(-not $monitorRejected){throw 'Relocation changed the requested monitor.'}
 Write-Host 'LongRun hover self-checks passed: native geometry target, no unchanged retry, one move per geometry, external motion rejected, monitor drift rejected.'
 Write-Host 'LongRun card validation self-checks passed: complete group, missing card rejected, duplicated HWND rejected.';exit 0
}
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
if($FixtureMode){
 if(-not $FixtureStatePath){throw 'Owned fixture state path required.'}
 Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @"
using System;
using System.IO;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
public sealed class AetherLongRunFixture : ApplicationContext {
 int remaining;
 [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
 AetherLongRunFixture(int count,int x,int y,string path){
  remaining=count;
  for(int index=0;index<count;++index){
   int current=index;
   var form=new Form{Text="Aether long-run fixture "+index.ToString("D2"),StartPosition=FormStartPosition.Manual,AutoScaleMode=AutoScaleMode.None,
    Location=new Point(x+(index%4)*28,y+(index%4)*24),ClientSize=new Size(500,280),BackColor=Color.FromArgb(40+index*6,80+index*3,140),FormBorderStyle=FormBorderStyle.FixedSingle};
   form.Controls.Add(new Label{Dock=DockStyle.Fill,Text="OWNED WORKLOAD WINDOW\n"+index.ToString("D2"),TextAlign=ContentAlignment.MiddleCenter,ForeColor=Color.White,Font=new Font("Segoe UI",24,FontStyle.Bold)});
   form.FormClosed+=(sender,args)=>{if(--remaining==0)ExitThread();};
   form.Show();ShowWindow(form.Handle,4);
   if(current==count-1)File.WriteAllText(path,"Ready");
  }
 }
 public static void Run(int count,int x,int y,string path){SetProcessDpiAwarenessContext((IntPtr)(-4));Application.EnableVisualStyles();using(var context=new AetherLongRunFixture(count,x,y,path))Application.Run(context);}
}
"@
 [AetherLongRunFixture]::Run($FixtureWindows,$FixtureX,$FixtureY,[IO.Path]::GetFullPath($FixtureStatePath));exit 0
}
$sourceRoot=Split-Path -Parent $PSScriptRoot;$stagePrefix=[IO.Path]::GetFullPath($sourceRoot).TrimEnd('\')+'\'
if(-not $BinaryPath){$BinaryPath=Join-Path $sourceRoot "build-v03\$Configuration\AetherUI.exe"}
$binary=[IO.Path]::GetFullPath($BinaryPath)
if(-not $binary.StartsWith($stagePrefix,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $binary -PathType Leaf)){throw 'Choose an existing candidate or preserved baseline executable inside this staged source tree.'}
if(-not $ArtifactDirectory){$ArtifactDirectory=Join-Path $sourceRoot ('docs\windows-validation\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-longrun-'+$PID)}
$run=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $run.StartsWith($stagePrefix,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $run)){throw 'ArtifactDirectory must be a fresh directory inside the staged source tree.'}
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class AetherLongRunProbe {
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]public struct Monitor{public int Size;public Rect Bounds,Work;public uint Flags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Name;}
 public sealed class Window{public IntPtr Handle;public string Title,Class;public uint ProcessId;public Rect Bounds;public bool Visible;}
 public delegate bool EnumProc(IntPtr window,IntPtr data);public delegate bool MonitorProc(IntPtr h,IntPtr dc,IntPtr rect,IntPtr data);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")]static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")]static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorProc callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool GetMonitorInfo(IntPtr h,ref Monitor info);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("kernel32.dll",SetLastError=true)]public static extern bool GetExitCodeProcess(IntPtr process,out uint code);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect bounds);
 [DllImport("user32.dll")]public static extern bool GetClientRect(IntPtr h,out Rect bounds);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point point);
 [DllImport("user32.dll")]public static extern IntPtr WindowFromPoint(Point point);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)]static extern IntPtr SendText(IntPtr h,uint msg,IntPtr w,string text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)]static extern IntPtr ReadText(IntPtr h,uint msg,IntPtr w,StringBuilder text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW")]static extern IntPtr SendValue(IntPtr h,uint msg,IntPtr w,IntPtr l,uint flags,uint timeout,out IntPtr result);
 public static bool Responsive(IntPtr h){IntPtr result;return h!=IntPtr.Zero&&SendValue(h,0,IntPtr.Zero,IntPtr.Zero,2,1000,out result)!=IntPtr.Zero;}
 [DllImport("dwmapi.dll")]static extern int DwmFlush();
 public static bool Owns(IntPtr h,int process){uint actual;return IsWindow(h)&&GetWindowThreadProcessId(h,out actual)!=0&&actual==(uint)process;}
 public static bool Text(IntPtr h,string text){IntPtr result;return h!=IntPtr.Zero&&SendText(h,0x000C,IntPtr.Zero,text,2,2000,out result)!=IntPtr.Zero&&result!=IntPtr.Zero;}
 public static string Text(IntPtr h){var text=new StringBuilder(512);IntPtr result;if(h==IntPtr.Zero||ReadText(h,0x000D,(IntPtr)512,text,2,2000,out result)==IntPtr.Zero)return null;return text.ToString();}
 public static Window Describe(IntPtr h){if(h==IntPtr.Zero||!IsWindow(h))return null;uint pid;GetWindowThreadProcessId(h,out pid);var title=new StringBuilder(512);var cls=new StringBuilder(256);GetWindowText(h,title,512);GetClassName(h,cls,256);Rect bounds;GetWindowRect(h,out bounds);return new Window{Handle=h,ProcessId=pid,Title=title.ToString(),Class=cls.ToString(),Bounds=bounds,Visible=IsWindowVisible(h)};}
 public static Window[] Windows(int process){var list=new List<Window>();EnumWindows((h,data)=>{uint pid;GetWindowThreadProcessId(h,out pid);if(process==0||pid==(uint)process){var text=new StringBuilder(512);var cls=new StringBuilder(256);GetWindowText(h,text,512);GetClassName(h,cls,256);Rect bounds;GetWindowRect(h,out bounds);list.Add(new Window{Handle=h,ProcessId=pid,Title=text.ToString(),Class=cls.ToString(),Bounds=bounds,Visible=IsWindowVisible(h)});}return true;},IntPtr.Zero);return list.ToArray();}
 public static Monitor MonitorFor(IntPtr window){var info=new Monitor();info.Size=Marshal.SizeOf(typeof(Monitor));if(!GetMonitorInfo(MonitorFromWindow(window,2),ref info))throw new InvalidOperationException("Native window monitor unavailable");return info;}
 public static Monitor[] Monitors(){var list=new List<Monitor>();EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,(h,dc,rect,data)=>{var info=new Monitor();info.Size=Marshal.SizeOf(typeof(Monitor));GetMonitorInfo(h,ref info);list.Add(info);return true;},IntPtr.Zero);return list.ToArray();}
 public static void Capture(IntPtr window,string path){Rect bounds;if(!GetWindowRect(window,out bounds)||bounds.Right<=bounds.Left||bounds.Bottom<=bounds.Top)throw new InvalidOperationException("No drawable native bounds");DwmFlush();using(var bitmap=new Bitmap(bounds.Right-bounds.Left,bounds.Bottom-bounds.Top)){using(var graphics=Graphics.FromImage(bitmap))graphics.CopyFromScreen(bounds.Left,bounds.Top,0,0,bitmap.Size,CopyPixelOperation.SourceCopy);bitmap.Save(path,ImageFormat.Png);}}
}
"@
[AetherLongRunProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
if([AetherLongRunProbe]::FindWindow('Aether.Controller.v3','Aether UI Controller') -ne [IntPtr]::Zero){throw 'Close the current Aether instance before isolated measurement. It was left unchanged.'}
$before=[AetherLongRunProbe]::Monitors();$taskbars=@([AetherLongRunProbe]::Windows(0)|Where-Object {$_.Class -in @('Shell_TrayWnd','Shell_SecondaryTrayWnd')})
if(-not $before.Count -or -not @($taskbars|Where-Object {$_.Visible}).Count){throw 'This workload requires an interactive desktop with a visible native taskbar.'}
$pointerBefore=New-Object Drawing.Point;[AetherLongRunProbe]::GetCursorPos([ref]$pointerBefore)|Out-Null
$profile=Join-Path $run 'profile';New-Item -ItemType Directory -Path $profile -Force|Out-Null
$powerShell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$settings="[AetherSettings]`nversion=3`n[behavior]`nhideTaskbar=0`nwindowPreviews=1`n[layout]`nedge=bottom`niconSize=48`ndockGap=10`ntopBar=1`ndock=1`nautoHide=0`nmonitorOnly=0`nreserveDock=1`n[pin:0]`nname=Owned%20workload%20windows`ntarget=$powerShell`nidentity=$($powerShell.ToLowerInvariant())`n"
[IO.File]::WriteAllText((Join-Path $profile 'settings-v3.ini'),$settings,(New-Object Text.UTF8Encoding $false))
$appProcess=$null;$fixtureProcess=$null;$measurement=$null;$controller=$null;$failure=$null
$script:pointerInterference=$false
$checks=[ordered]@{ActivitiesCycles=0;OverviewCycles=0;EditorCycles=0;DockHoverCycles=0;DockFlyoutCycles=0;PostInteractionPopupsHidden=$false}
$dockCycleDiagnostics=New-Object 'System.Collections.Generic.List[object]';$measurementDiagnostics=[ordered]@{};$ownedHelpers=@{};$cleanupFailures=New-Object 'System.Collections.Generic.List[string]';$samples=[ordered]@{}
function Own-Post($window,[uint32]$message,[int64]$w=0,[int64]$l=0){
 if(-not [AetherLongRunProbe]::Owns($window.Handle,$appProcess.Id)){throw 'Native action HWND/PID no longer belongs to the owned Aether process.'}
 if(-not [AetherLongRunProbe]::PostMessage($window.Handle,$message,[IntPtr]$w,[IntPtr]$l)){throw 'Could not dispatch native owned-window action.'}
}
function Wait-Window($process,[string]$title,[bool]$visible=$true,[int]$timeout=15){
 $deadline=[datetime]::UtcNow.AddSeconds($timeout)
 do{$process.Refresh();if($process.HasExited){throw "Owned process exited while waiting for $title"};$windows=@([AetherLongRunProbe]::Windows($process.Id)|Where-Object {$_.Title -eq $title -and (-not $visible -or $_.Visible)});if($windows.Count){return $windows[0]};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow -lt $deadline)
 $diagnosticWindows=@([AetherLongRunProbe]::Windows($process.Id)|Select-Object -First 16)
 $diagnostic=@($diagnosticWindows|ForEach-Object {[pscustomobject]@{Handle=$_.Handle.ToInt64();Title=$_.Title;Class=$_.Class;Visible=$_.Visible;Bounds=$_.Bounds;Responsive=[AetherLongRunProbe]::Responsive($_.Handle)}})
 $diagnostic|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run ('window-timeout-'+$process.Id+'.json')) -Encoding utf8
 throw "Owned native window did not appear: $title"
}
function Assert-Taskbars {foreach($taskbar in $taskbars){if($taskbar.Visible -and -not [AetherLongRunProbe]::IsWindowVisible($taskbar.Handle)){throw 'A visible native taskbar became hidden.'}}}
function Remember-Helpers {
 if(-not $appProcess){return};$appProcess.Refresh();if($appProcess.HasExited){return}
 foreach($child in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($appProcess.Id)" -ErrorAction SilentlyContinue)){
  if($child.Name -notin @('AetherProvider.exe','AetherWatchdog.exe','AetherWallpaperProvider.exe') -or -not $child.CreationDate){continue}
  $process=Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue
  if($process){try{$created=$process.StartTime.ToUniversalTime();if($created -ge $appStartUtc -and [Math]::Abs(($created-([datetime]$child.CreationDate).ToUniversalTime()).Ticks) -le 10 -and ($process.ProcessName+'.exe') -ieq $child.Name){$identity='{0}:{1}' -f $process.Id,$created.Ticks;$ownedHelpers[$identity]=[pscustomobject]@{Identity=$identity;ProcessId=$process.Id;StartUtc=$created.ToString('o');StartTicks=$created.Ticks;Name=$process.ProcessName}}}catch{}finally{$process.Dispose()}}
 }
}
function Write-HandshakeFile([string]$path,$data){
 $temporary=$path+'.tmp-'+[guid]::NewGuid().ToString('N')
 try{[IO.File]::WriteAllText($temporary,($data|ConvertTo-Json -Depth 4 -Compress),(New-Object Text.UTF8Encoding $false));[IO.File]::Move($temporary,$path)}finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
}
function Start-Measurement([string]$scenario,[int]$seconds){
 $output=Join-Path $run ($scenario.ToLowerInvariant()+'.json')
 $readyPath=Join-Path $run ($scenario.ToLowerInvariant()+'-ready.json');$startPath=Join-Path $run ($scenario.ToLowerInvariant()+'-start.json')
 $script:measurement=Start-Process -FilePath $powerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'Measure.ps1')+'"'),'-Seconds',$seconds,'-Interval',$Interval,'-Scenario',$scenario,'-RootProcessId',$appProcess.Id,'-OutputPath',('"'+$output+'"'),'-ReadyPath',('"'+$readyPath+'"'),'-StartPath',('"'+$startPath+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run ($scenario.ToLowerInvariant()+'.log')) -RedirectStandardError (Join-Path $run ($scenario.ToLowerInvariant()+'-error.log'))
 $null=$measurement.Handle;$deadline=[datetime]::UtcNow.AddSeconds(15)
 while(-not (Test-Path -LiteralPath $readyPath -PathType Leaf)){
  if($measurement.WaitForExit(25)){throw 'Measurement worker exited before its baseline-ready handshake; inspect phase error log.'}
  if([datetime]::UtcNow -gt $deadline){throw 'Measurement worker did not finish its initial baseline within 15 seconds.'}
 }
 $ready=Get-Content -LiteralPath $readyPath -Raw|ConvertFrom-Json
 $expectedIdentity='{0}:{1}' -f $appProcess.Id,$appStartUtc.Ticks
 if($ready.WorkerProcessId -ne $measurement.Id -or $ready.RootIdentity -ne $expectedIdentity -or $ready.Scenario -ne $scenario){throw 'Sampler readiness does not match the owned worker/root/scenario.'}
 $script:measurementStartUtc=[datetime]::UtcNow
 Write-HandshakeFile $startPath ([pscustomobject]@{WorkerProcessId=$measurement.Id;RootIdentity=$expectedIdentity;Scenario=$scenario;StartUtc=$measurementStartUtc.ToString('o')})
 return $output
}
function Owned-ExitCode($ownedProcess){
 if(-not $ownedProcess.WaitForExit(1000)){throw 'Owned process is still active while reading its exit code.'}
 $code=[uint32]0;if(-not [AetherLongRunProbe]::GetExitCodeProcess($ownedProcess.Handle,[ref]$code)){throw 'Native owned-process exit code is unavailable.'}
 return [int]$code
}
function Finish-Measurement([string]$path,[int]$requested){
 if(-not $measurement.WaitForExit(15000)){throw 'Owned measurement worker did not exit within its bounded completion window.'}
 if((Owned-ExitCode $measurement) -ne 0 -or -not (Test-Path -LiteralPath $path)){throw 'Measurement worker failed; inspect its output/error logs.'}
 $result=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
 $measurementDiagnostics[$result.Scenario]=[pscustomobject]@{DiscoveryFailures=@($result.DiscoveryFailures);LateFirstSightings=$result.LateFirstSightings;FinalObservedExits=$result.FinalObservedExits;RequestedIntervalSeconds=$result.RequestedIntervalSeconds;BaselineUtc=$result.BaselineUtc;ReadyUtc=$result.ReadyUtc;StartSignalUtc=$result.StartSignalUtc;MeasurementStartUtc=$result.MeasurementStartUtc;MeasurementEndUtc=$result.MeasurementEndUtc;InitialBaselineLeadSeconds=$result.InitialBaselineLeadSeconds;CpuIntervalSeconds=$result.CpuIntervalSeconds;SamplingLimits=$result.SamplingLimits}
 if(@($result.DiscoveryFailures).Count){throw 'Process-tree discovery failed; the phase is incomplete. See MeasurementDiagnostics and the raw phase JSON.'}
 if(-not $result.Samples.Count -or $result.ActualSeconds -lt $requested -or $result.Samples[-1].ProcessCount -lt 1){throw 'Measurement did not contain the requested duration of live-app samples.'}
 foreach($observed in $result.ObservedProcesses){if($observed.Name -in @('AetherProvider','AetherWatchdog','AetherWallpaperProvider')){$key=$observed.Identity;$ownedHelpers[$key]=[pscustomobject]@{Identity=$key;ProcessId=$observed.ProcessId;StartUtc=$observed.StartUtc;StartTicks=([datetime]$observed.StartUtc).ToUniversalTime().Ticks;Name=$observed.Name}}}
 $measurement.Dispose();$script:measurement=$null;return $result
}
function Idle-Measurement([string]$scenario,[int]$seconds){
 $path=Start-Measurement $scenario $seconds;$deadline=[datetime]::UtcNow.AddSeconds($seconds+30)
 while(-not $measurement.WaitForExit(1000)){Assert-Taskbars;if([datetime]::UtcNow -gt $deadline){throw 'Idle measurement exceeded its bounded duration.'}}
 return Finish-Measurement $path $seconds
}
function Hide-OwnedPopups {
 foreach($window in @([AetherLongRunProbe]::Windows($appProcess.Id)|Where-Object {$_.Visible -and $_.Title -in @('Aether Activities','Aether UI - Desktop Studio','Aether Dock Windows')})){Own-Post $window 0x0010}
 Start-Sleep -Milliseconds 250
}
function Click-Dips($window,[double]$x,[double]$y){$scale=[AetherLongRunProbe]::GetDpiForWindow($window.Handle)/96.0;Own-Post $window 0x0202 0 (([int]($y*$scale) -shl 16) -bor ([int]($x*$scale) -band 0xFFFF))}
function Dock-Snapshot([string]$stage){
 $pointer=New-Object Drawing.Point;[AetherLongRunProbe]::GetCursorPos([ref]$pointer)|Out-Null
 $windows=@([AetherLongRunProbe]::Windows($appProcess.Id)|Select-Object -First 48)
 return [pscustomobject]@{Stage=$stage;Utc=[datetime]::UtcNow.ToString('o');CursorX=$pointer.X;CursorY=$pointer.Y;WindowAtCursor=[AetherLongRunProbe]::WindowFromPoint($pointer).ToInt64();CursorWindow=[AetherLongRunProbe]::Describe([AetherLongRunProbe]::WindowFromPoint($pointer));ForegroundWindow=[AetherLongRunProbe]::GetForegroundWindow().ToInt64();Windows=@($windows|ForEach-Object {[pscustomobject]@{Handle=$_.Handle.ToInt64();Title=$_.Title;Class=$_.Class;ProcessId=$_.ProcessId;Parent=[AetherLongRunProbe]::GetParent($_.Handle).ToInt64();Visible=$_.Visible;Bounds=$_.Bounds;Dpi=[AetherLongRunProbe]::GetDpiForWindow($_.Handle)}})}
}
function Wait-DockFlyout($dock,$cycle){
 $deadline=[datetime]::UtcNow.AddMilliseconds($(if($RequireDockFlyout){2000}else{550}));$last=$null
 $cycle.HoverDeadlineUtc=$deadline.ToString('o')
 $seenGeometry=New-Object 'System.Collections.Generic.HashSet[string]';$seenGeometry.Add((Get-DockGeometryKey $cycle.RequestedHover))|Out-Null
 do{
  $last=Dock-Snapshot 'flyout-poll'
  try{
   Assert-DockPointerUnchanged $last $cycle.RequestedHover $cycle
   if(-not [AetherLongRunProbe]::Owns($dock.Handle,$appProcess.Id)){throw 'Requested native dock HWND/PID ownership changed during hover.'}
   $currentDock=[AetherLongRunProbe]::Describe($dock.Handle)
   if(-not $currentDock.Visible -or $currentDock.Title -ne 'Aether Dock' -or $currentDock.Class -ne 'Aether.Surface.v3'){throw 'Requested native dock became unavailable during hover.'}
   $currentClient=New-Object AetherLongRunProbe+Rect
   if(-not [AetherLongRunProbe]::GetClientRect($dock.Handle,[ref]$currentClient)){throw 'Requested dock client geometry became unavailable.'}
   $currentTarget=Get-DockHoverTarget ([pscustomobject]@{Handle=$dock.Handle.ToInt64();Bounds=$currentDock.Bounds;Client=$currentClient;Scale=[AetherLongRunProbe]::GetDpiForWindow($dock.Handle)/96.0;Monitor=[AetherLongRunProbe]::MonitorFor($dock.Handle)})
   $relocation=Get-DockHoverRelocation $currentTarget $cycle.RequestedHover $last $seenGeometry
   if($null -ne $relocation){
    # Geometry is native and changed. Recheck real input immediately before one move.
    $pointer=New-Object Drawing.Point
    if(-not [AetherLongRunProbe]::GetCursorPos([ref]$pointer)){throw 'Real cursor position became unavailable before relocation.'}
    Assert-DockPointerUnchanged ([pscustomobject]@{CursorX=$pointer.X;CursorY=$pointer.Y}) $cycle.RequestedHover $cycle
    if([datetime]::UtcNow -ge $deadline){$cycle.AfterHover=$last;return $null}
    $previous=$cycle.RequestedHover
    $relocationUtc=[datetime]::UtcNow.ToString('o')
    $moved=[AetherLongRunProbe]::SetCursorPos($relocation.ScreenX,$relocation.ScreenY)
    $cycle.RequestedHover=$relocation
    $afterMove=Dock-Snapshot 'geometry-relocation'
    $cycle.HoverRelocations+=@([pscustomobject]@{Utc=$relocationUtc;PreviousBounds=$previous.Bounds;CurrentBounds=$relocation.Bounds;PreviousTarget=$previous;CurrentTarget=$relocation;CursorBefore=[pscustomobject]@{X=$pointer.X;Y=$pointer.Y};CursorAfter=[pscustomobject]@{X=$afterMove.CursorX;Y=$afterMove.CursorY};WindowAtCursor=$afterMove.WindowAtCursor;SetCursorSucceeded=$moved})
    $last=$afterMove
    Assert-DockPointerUnchanged $last $cycle.RequestedHover $cycle
    if(-not $moved -or $last.WindowAtCursor -ne $dock.Handle.ToInt64() -or -not [AetherLongRunProbe]::Owns([IntPtr]$last.WindowAtCursor,$appProcess.Id)){throw 'Relocated real cursor did not reach the requested owned native dock.'}
   }
  }catch{
   $cycle.AfterHover=$last
   $pointer=New-Object Drawing.Point
   if([AetherLongRunProbe]::GetCursorPos([ref]$pointer) -and ($pointer.X -ne $cycle.RequestedHover.ScreenX -or $pointer.Y -ne $cycle.RequestedHover.ScreenY)){$script:pointerInterference=$true;$cycle.ExternalCursorMovement=[pscustomobject]@{Utc=[datetime]::UtcNow.ToString('o');ExpectedX=$cycle.RequestedHover.ScreenX;ExpectedY=$cycle.RequestedHover.ScreenY;ActualX=$pointer.X;ActualY=$pointer.Y}}
   $cycle.HoverFailure=$_.Exception.Message
   $cycle|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run ('dock-hover-failure-cycle-'+$cycle.Index+'.json')) -Encoding utf8
   $dockCycleDiagnostics.Add([pscustomobject]$cycle);throw
  }

  $tooltips=@($last.Windows|Where-Object {$_.Visible -and $_.Class -eq 'tooltips_class32' -and $_.Parent -eq $dock.Handle.ToInt64() -and $_.ProcessId -eq $appProcess.Id})
  if($RequireDockFlyout -and $tooltips.Count){
   $cycle.UnexpectedLiveGroupTooltips=$tooltips;$cycle.AfterHover=$last
   $cycle|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run ('dock-tooltip-failure-cycle-'+$cycle.Index+'.json')) -Encoding utf8
   $dockCycleDiagnostics.Add([pscustomobject]$cycle);throw 'Requested live dock group displayed its standard tooltip while awaiting the native flyout.'
  }
  $matches=@($last.Windows|Where-Object {$_.Title -eq 'Aether Dock Windows' -and $_.Visible -and $_.ProcessId -eq $appProcess.Id})
  foreach($match in $matches){
   $native=[AetherLongRunProbe]::Describe([IntPtr]$match.Handle);$monitor=[AetherLongRunProbe]::MonitorFor($native.Handle)
   if($monitor.Name -ne $cycle.RequestedHover.Monitor.Name){continue}
   $bounds=$native.Bounds;$area=$monitor.Bounds
   if($bounds.Left -lt $area.Left -or $bounds.Right -gt $area.Right -or $bounds.Top -lt $area.Top -or $bounds.Bottom -gt $cycle.RequestedHover.Bounds.Top+24*$cycle.RequestedHover.Scale){continue}
   $cards=@();for($slot=0;$slot -lt 6;++$slot){$card=[AetherLongRunProbe]::GetDlgItem($native.Handle,100+$slot);if($card -ne [IntPtr]::Zero -and [AetherLongRunProbe]::IsWindowVisible($card)){$description=[AetherLongRunProbe]::Describe($card);$source=@($fixtureSources|Where-Object {$description.Title -eq $_.Title -or $description.Title -eq ($_.Title+' (Minimized)')});$cards+=[pscustomobject]@{Handle=$card.ToInt64();Title=$description.Title;ProcessId=$description.ProcessId;ExpectedSources=@($source|ForEach-Object {[pscustomobject]@{Handle=$_.Handle.ToInt64();ProcessId=$_.ProcessId;Title=$_.Title;Alive=[AetherLongRunProbe]::Owns($_.Handle,$fixtureProcess.Id)}})}}}
   $expectedCards=[Math]::Min(6,$fixtureSources.Count)
   $cycle.ExpectedVisibleCards=$expectedCards;$cycle.ActualVisibleCards=$cards.Count
   $cycle.FlyoutEvidence=[pscustomobject]@{ExpectedVisibleCards=$expectedCards;ActualVisibleCards=$cards.Count;DistinctFixtureHandles=@($cards|ForEach-Object {$_.ExpectedSources|ForEach-Object {$_.Handle}}|Select-Object -Unique);Handle=$native.Handle.ToInt64();ProcessId=$native.ProcessId;Title=$native.Title;Monitor=$monitor;Bounds=$native.Bounds;Cards=$cards}
   try{Assert-DockGroupCards $cards $expectedCards $appProcess.Id $fixtureProcess.Id}catch{$cycle.AfterHover=$last;$cycle|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run ('dock-group-failure-cycle-'+$cycle.Index+'.json')) -Encoding utf8;$dockCycleDiagnostics.Add([pscustomobject]$cycle);throw}
   $cycle.AfterHover=$last;return $native
  }
  Start-Sleep -Milliseconds 50
 }while([datetime]::UtcNow -lt $deadline)
 $cycle.AfterHover=$last;return $null
}
function Interaction-Cycle([int]$index){
 Own-Post $controller 0x0312 1;$activities=Wait-Window $appProcess 'Aether Activities';$edit=[AetherLongRunProbe]::FindWindowEx($activities.Handle,[IntPtr]::Zero,'EDIT',$null)
 Click-Dips $activities 95 120
 $query=@('terminal','code','settings','')[($index%4)]
 if(-not [AetherLongRunProbe]::Text($edit,$query) -or [AetherLongRunProbe]::Text($edit) -ne $query){throw 'Native Activities search did not accept the workload query.'}
 Start-Sleep -Milliseconds 120;Own-Post ([pscustomobject]@{Handle=$edit}) 0x0100 0x28;++$checks.ActivitiesCycles
 Click-Dips $activities 240 120
 if(-not [AetherLongRunProbe]::Text($edit,'Aether long-run fixture')){throw 'Native overview search query was not accepted.'}
 Start-Sleep -Milliseconds 120
  # Focus the grid on accessible candidates; legacy versions ignore this Tab.
 Own-Post $activities 0x0100 0x09
 foreach($key in @(0x27,0x28,0x22)){Own-Post $activities 0x0100 $key}
 if($index -eq 0){
  # Visual QA only: let the app drain posted navigation and repaint before DwmFlush.
  # Performance comparisons retain the immediate capture path by default.
  if($SettledVisualCapture){Start-Sleep -Milliseconds 500}
  [AetherLongRunProbe]::Capture($activities.Handle,(Join-Path $run 'overview-workload.png'))
 }
 ++$checks.OverviewCycles;Own-Post $activities 0x0010;Start-Sleep -Milliseconds 100
 Own-Post $controller 0x0312 2;$editor=Wait-Window $appProcess 'Aether UI - Desktop Studio'
 $client=New-Object AetherLongRunProbe+Rect;[AetherLongRunProbe]::GetClientRect($editor.Handle,[ref]$client)|Out-Null
 $dpiScale=[AetherLongRunProbe]::GetDpiForWindow($editor.Handle)/96.0;$layoutScale=[Math]::Min(1.0,[Math]::Max(.1,$client.Right/$dpiScale/730.0))
 Click-Dips $editor (95*$layoutScale) (175*$layoutScale)
 foreach($key in @(0x09,0x22,0x21)){Own-Post $editor 0x0100 $key}
 if($index -eq 0){[AetherLongRunProbe]::Capture($editor.Handle,(Join-Path $run 'editor-workload.png'))}
 ++$checks.EditorCycles;Own-Post $editor 0x0010;Start-Sleep -Milliseconds 100
 $dock=@([AetherLongRunProbe]::Windows($appProcess.Id)|Where-Object {$_.Title -eq 'Aether Dock' -and $_.Visible}|Sort-Object {$_.Bounds.Left})[0]
 $scale=[AetherLongRunProbe]::GetDpiForWindow($dock.Handle)/96.0;$client=New-Object AetherLongRunProbe+Rect;[AetherLongRunProbe]::GetClientRect($dock.Handle,[ref]$client)|Out-Null
 $hoverX=[int](96*$scale);$hoverY=[int]($client.Bottom/2)
 $moved=[AetherLongRunProbe]::SetCursorPos($dock.Bounds.Left+$hoverX,$dock.Bounds.Top+$hoverY)
 $cycle=[ordered]@{Index=$index;ActivitiesTab='Open windows';OverviewQuery='Aether long-run fixture';RequestedHover=[pscustomobject]@{Handle=$dock.Handle.ToInt64();Bounds=$dock.Bounds;Client=$client;Scale=$scale;Monitor=[AetherLongRunProbe]::MonitorFor($dock.Handle);LocalX=$hoverX;LocalY=$hoverY;ScreenX=$dock.Bounds.Left+$hoverX;ScreenY=$dock.Bounds.Top+$hoverY};SetCursorSucceeded=$moved;BeforeHover=(Dock-Snapshot 'before-hover');AfterHover=$null;OriginalRequestedHover=$null;HoverRelocations=@();TooltipSuppressionChecked=$RequireDockFlyout.IsPresent;UnexpectedLiveGroupTooltips=@();ExpectedVisibleCards=[Math]::Min(6,$fixtureSources.Count);ActualVisibleCards=0;FlyoutEvidence=$null;ExpectedFixtureProcessId=$fixtureProcess.Id;ExpectedFixtureStartUtc=$fixtureProcess.StartTime.ToUniversalTime().ToString('o');ExpectedFixtureWindows=@($fixtureSources|ForEach-Object {[pscustomobject]@{Handle=$_.Handle.ToInt64();ProcessId=$_.ProcessId;Title=$_.Title}});Opened=$false}
 $cycle.OriginalRequestedHover=$cycle.RequestedHover
 if(-not $moved -or $cycle.BeforeHover.CursorX -ne $cycle.RequestedHover.ScreenX -or $cycle.BeforeHover.CursorY -ne $cycle.RequestedHover.ScreenY -or $cycle.BeforeHover.WindowAtCursor -ne $dock.Handle.ToInt64() -or -not [AetherLongRunProbe]::Owns([IntPtr]$cycle.BeforeHover.WindowAtCursor,$appProcess.Id)){
  if($cycle.BeforeHover.CursorX -ne $cycle.RequestedHover.ScreenX -or $cycle.BeforeHover.CursorY -ne $cycle.RequestedHover.ScreenY){$script:pointerInterference=$true}
  $cycle|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run ('dock-pointer-failure-cycle-'+$index+'.json')) -Encoding utf8
  $dockCycleDiagnostics.Add([pscustomobject]$cycle);throw 'Real cursor hover did not reach the owned native dock window.'
 }
 ++$checks.DockHoverCycles;$flyout=Wait-DockFlyout $dock $cycle
 if($flyout){
  $cycle.Opened=$true
  ++$checks.DockFlyoutCycles
  if($index -eq 0){[AetherLongRunProbe]::Capture($flyout.Handle,(Join-Path $run 'dock-workload.png'))}
  Own-Post $flyout 0x0100 0x22;Start-Sleep -Milliseconds 100;Own-Post $flyout 0x0010
 }elseif($RequireDockFlyout){
  $cycle|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run ('dock-failure-cycle-'+$index+'.json')) -Encoding utf8
  [AetherLongRunProbe]::Capture($dock.Handle,(Join-Path $run ('dock-failure-cycle-'+$index+'.png')))
  $dockCycleDiagnostics.Add([pscustomobject]$cycle)
  throw 'Required native dock flyout did not open for the owned grouped-window target.'
 }
 try{
  $pointer=New-Object Drawing.Point
  if(-not [AetherLongRunProbe]::GetCursorPos([ref]$pointer)){throw 'Real cursor position became unavailable before leaving the dock.'}
  Assert-DockPointerUnchanged ([pscustomobject]@{CursorX=$pointer.X;CursorY=$pointer.Y}) $cycle.RequestedHover $cycle
 }catch{
  $cycle.HoverFailure=$_.Exception.Message;$cycle|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $run ('dock-hover-failure-cycle-'+$index+'.json')) -Encoding utf8
  $dockCycleDiagnostics.Add([pscustomobject]$cycle);throw
 }
 $dockCycleDiagnostics.Add([pscustomobject]$cycle)
 if(-not [AetherLongRunProbe]::SetCursorPos($before[0].Work.Left+16,$before[0].Work.Top+16)){throw 'Could not move the real cursor away from the dock.'}
 Start-Sleep -Milliseconds 150;Remember-Helpers;Assert-Taskbars
}
function Wait-Restored {
 $initial=$before|ConvertTo-Json -Depth 4 -Compress;$deadline=[datetime]::UtcNow.AddSeconds(5)
 do{$latest=[AetherLongRunProbe]::Monitors();if(($latest|ConvertTo-Json -Depth 4 -Compress) -eq $initial){return [pscustomobject]@{Restored=$true;Monitors=$latest}};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow -lt $deadline)
 return [pscustomobject]@{Restored=$false;Monitors=$latest}
}
try{
 $fixtureState=Join-Path $run 'fixture-state.txt';$work=$before[0].Work
 $fixtureProcess=Start-Process -FilePath $powerShell -ArgumentList @('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-FixtureMode','-FixtureStatePath',('"'+$fixtureState+'"'),'-FixtureWindows',$FixtureWindows,'-FixtureX',($work.Left+40),'-FixtureY',($work.Top+90)) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'fixture.log') -RedirectStandardError (Join-Path $run 'fixture-error.log')
 $null=$fixtureProcess.Handle;$fixture=Wait-Window $fixtureProcess 'Aether long-run fixture 00'
 $deadline=[datetime]::UtcNow.AddSeconds(15)
 do{$fixtureCount=@([AetherLongRunProbe]::Windows($fixtureProcess.Id)|Where-Object {$_.Visible -and $_.Title -like 'Aether long-run fixture *'}).Count;if($fixtureCount -eq $FixtureWindows){break};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow -lt $deadline)
 if($fixtureCount -ne $FixtureWindows){throw 'Foreign fixture did not create the complete owned grouped-window workload.'}
 $fixtureSources=@([AetherLongRunProbe]::Windows($fixtureProcess.Id)|Where-Object {$_.Visible -and $_.Title -like 'Aether long-run fixture *'})
 [AetherLongRunProbe]::SetForegroundWindow($fixture.Handle)|Out-Null
 $appProcess=Start-Process -FilePath $binary -ArgumentList @('--settings-dir',('"'+$profile+'"')) -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $run 'app-output.log') -RedirectStandardError (Join-Path $run 'app-error.log')
 $null=$appProcess.Handle;$appStartUtc=$appProcess.StartTime.ToUniversalTime();$controller=Wait-Window $appProcess 'Aether UI Controller' $false
 $dock=Wait-Window $appProcess 'Aether Dock';Hide-OwnedPopups;if(-not [AetherLongRunProbe]::SetCursorPos($before[0].Work.Left+16,$before[0].Work.Top+16)){throw 'Could not place the real cursor away from the dock before measurement.'};Start-Sleep -Seconds $WarmupSeconds;Assert-Taskbars
 $samples.Idle=Idle-Measurement 'Idle' $IdleSeconds
 $path=Start-Measurement 'Interaction' $InteractionSeconds;$interactionDeadline=$measurementStartUtc.AddSeconds($InteractionSeconds+30);$index=0
 while(-not $measurement.WaitForExit(0)){
  if([datetime]::UtcNow -gt $interactionDeadline){throw 'Interaction sampler exceeded its bounded handshake-aligned duration.'}
  Interaction-Cycle $index;++$index
 }
 $samples.Interaction=Finish-Measurement $path $InteractionSeconds
 Hide-OwnedPopups;$popups=@([AetherLongRunProbe]::Windows($appProcess.Id)|Where-Object {$_.Visible -and $_.Title -in @('Aether Activities','Aether UI - Desktop Studio','Aether Dock Windows')})
 $checks.PostInteractionPopupsHidden=$popups.Count -eq 0;if(-not $checks.PostInteractionPopupsHidden){throw 'Interaction popups remained visible before retained-memory sampling.'}
 $samples.PostInteraction=Idle-Measurement 'PostInteraction' $PostInteractionSeconds
 Remember-Helpers;Assert-Taskbars
}catch{$failure=$_}finally{
 if($measurement){try{if(-not $measurement.WaitForExit(15000)){$cleanupFailures.Add('Measurement worker remained running after bounded cleanup; it was left for diagnosis.')};$measurement.Dispose()}catch{$cleanupFailures.Add($_.Exception.Message)}}
 $appExitCode=$null;$appNormalExit=$false;$appExitedUtc=$null
 if($appProcess){
  try{
   $appProcess.Refresh();if(-not $appProcess.HasExited){foreach($window in @([AetherLongRunProbe]::Windows($appProcess.Id)|Where-Object {$_.Title -eq 'Aether UI Controller'})){Own-Post $window 0x0010};if(-not $appProcess.WaitForExit(15000)){$cleanupFailures.Add('Owned Aether did not exit within 15 seconds; it was left running for diagnosis.')}}
   if($appProcess.HasExited){$appExitCode=Owned-ExitCode $appProcess;$appExitedUtc=$appProcess.ExitTime.ToUniversalTime();$appNormalExit=$appExitCode -eq 0;if(-not $appNormalExit){$cleanupFailures.Add('Owned Aether returned a nonzero exit code.')}}
  }catch{$cleanupFailures.Add($_.Exception.Message)}
 }
 if($fixtureProcess){
  try{
   $fixtureProcess.Refresh();if(-not $fixtureProcess.HasExited){foreach($window in @([AetherLongRunProbe]::Windows($fixtureProcess.Id)|Where-Object {$_.Title -like 'Aether long-run fixture *'})){if([AetherLongRunProbe]::Owns($window.Handle,$fixtureProcess.Id)){[AetherLongRunProbe]::PostMessage($window.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}};if(-not $fixtureProcess.WaitForExit(10000)){$cleanupFailures.Add('Owned fixture did not exit within 10 seconds; it was left running for diagnosis.')}}
   if($fixtureProcess.HasExited -and (Owned-ExitCode $fixtureProcess) -ne 0){$cleanupFailures.Add('Owned fixture returned a nonzero exit code.')}
  }catch{$cleanupFailures.Add($_.Exception.Message)}
 }
 # Include a final direct-child discovery born during the owned root lifetime. Never kill a matching name or a reused PID.
 if($appProcess -and $appExitedUtc){
  foreach($child in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($appProcess.Id)" -ErrorAction SilentlyContinue)){
   if($child.Name -in @('AetherProvider.exe','AetherWatchdog.exe','AetherWallpaperProvider.exe') -and $child.CreationDate){$created=([datetime]$child.CreationDate).ToUniversalTime();if($created -ge $appStartUtc -and $created -le $appExitedUtc){$process=Get-Process -Id ([int]$child.ProcessId) -ErrorAction SilentlyContinue;if($process){try{$liveStart=$process.StartTime.ToUniversalTime();if([Math]::Abs(($liveStart-$created).Ticks) -le 10){$key='{0}:{1}' -f $process.Id,$liveStart.Ticks;$ownedHelpers[$key]=[pscustomobject]@{Identity=$key;ProcessId=$process.Id;StartUtc=$liveStart.ToString('o');StartTicks=$liveStart.Ticks;Name=$process.ProcessName}}}catch{}finally{$process.Dispose()}}}}
  }
 }
 $orphans=New-Object 'System.Collections.Generic.List[object]';$deadline=[datetime]::UtcNow.AddSeconds(5)
 do{$orphans.Clear();foreach($helper in $ownedHelpers.Values){$process=Get-Process -Id $helper.ProcessId -ErrorAction SilentlyContinue;if($process){try{if($process.StartTime.ToUniversalTime().Ticks -eq $helper.StartTicks){$orphans.Add($helper)}}catch{}finally{$process.Dispose()}}};if(-not $orphans.Count){break};Start-Sleep -Milliseconds 100}while([datetime]::UtcNow -lt $deadline)
 if($orphans.Count){$cleanupFailures.Add('An observed owned helper process generation remained after normal app exit; no unrelated process was terminated.')}
 $restoration=Wait-Restored;$taskbarsVisible=@($taskbars|Where-Object {$_.Visible -and -not [AetherLongRunProbe]::IsWindowVisible($_.Handle)}).Count -eq 0
 if(-not $restoration.Restored){$cleanupFailures.Add('Native monitor work areas did not return to their pre-run snapshot.')};if(-not $taskbarsVisible){$cleanupFailures.Add('A native taskbar remained hidden.')}
 if(-not $pointerInterference){[AetherLongRunProbe]::SetCursorPos($pointerBefore.X,$pointerBefore.Y)|Out-Null}
 $summary=[ordered]@{}
 foreach($name in $samples.Keys){$sample=$samples[$name];$summary[$name]=[pscustomobject]@{ActualSeconds=$sample.ActualSeconds;AverageCpuPercentOfMachine=$sample.AverageCpuPercentOfMachine;AverageCpuPercentOfOneCore=$sample.AverageCpuPercentOfOneCore;PeakSampleCpuPercentOfMachine=$sample.PeakSampleCpuPercentOfMachine;AveragePrivateMiB=$sample.AveragePrivateMiB;PeakPrivateMiB=$sample.PeakPrivateMiB;PrivateGrowthMiB=$sample.PrivateGrowthMiB;PrivateSlopeMiBPerMinute=$sample.PrivateSlopeMiBPerMinute;PeakProcessCount=$sample.PeakProcessCount;FinalObservedExits=$sample.FinalObservedExits}}
 [pscustomobject]@{Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash;Configuration=$Configuration;Profile=$profile;FixtureWindows=$FixtureWindows;WarmupSeconds=$WarmupSeconds;IdleSeconds=$IdleSeconds;InteractionSeconds=$InteractionSeconds;PostInteractionSeconds=$PostInteractionSeconds;Checks=$checks;DockCycleDiagnostics=$dockCycleDiagnostics.ToArray();Measurements=$summary;MeasurementDiagnostics=$measurementDiagnostics;PostVsInitialPrivateMiB=$(if($samples.Contains('Idle') -and $samples.Contains('PostInteraction')){$samples.PostInteraction.LastLivePrivateMiB-$samples.Idle.LastLivePrivateMiB}else{$null});Success=($null -eq $failure -and $cleanupFailures.Count -eq 0);Failure=$(if($failure){$failure.Exception.Message}else{$null});Limits='Real native owned-window messages and real cursor hover. Fixture/probe costs are excluded from process scope but share machine scheduling. Baseline without a flyout records zero flyout cycles unless RequireDockFlyout is set. No display settings, session lock, Explorer restart, fullscreen game, GPU, DWM, temperature or frame-time validation.'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'runtime.json') -Encoding utf8
 [pscustomobject]@{NormalExit=$appNormalExit;ExitCode=$appExitCode;AppProcessId=$(if($appProcess){$appProcess.Id}else{$null});AppStartUtc=$(if($appProcess){$appStartUtc.ToString('o')}else{$null});ObservedHelpers=@($ownedHelpers.Values);OrphanHelpers=$orphans.ToArray();FixtureNormalExit=($fixtureProcess -and $fixtureProcess.HasExited -and (Owned-ExitCode $fixtureProcess) -eq 0);WorkAreasRestored=$restoration.Restored;TaskbarsVisible=$taskbarsVisible;MonitorsBefore=$before;MonitorsAfter=$restoration.Monitors;CleanupFailures=$cleanupFailures.ToArray()}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $run 'exit.json') -Encoding utf8
 if($appProcess){$appProcess.Dispose()};if($fixtureProcess){$fixtureProcess.Dispose()}
 Write-Host "Long-run artifacts: $run"
}
if($failure){throw $failure};if($cleanupFailures.Count){throw ($cleanupFailures -join ' ')}
Write-Host 'Long native workloads, scoped process measurements and normal exit/restoration checks passed.'




