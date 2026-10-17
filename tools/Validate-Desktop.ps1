[CmdletBinding()]
param([ValidateSet('Release','Debug')][string]$Configuration='Release',[int]$Seconds=30,[switch]$SkipMeasurements,[switch]$TestInstalledAppLaunch,[string]$BinaryPath="",[string]$ArtifactLabel="")
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$binary=Join-Path $root "build-v03\$Configuration\AetherUI.exe"
if($BinaryPath){$binary=[IO.Path]::GetFullPath($BinaryPath)}
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class AetherProbe {
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct Monitor {public int Size;public Rect Bounds,Work;public uint Flags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Name;}
 public class Window {public IntPtr Handle;public string Title;public Rect Bounds;public bool Visible;}
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 public delegate bool MonitorProc(IntPtr h,IntPtr dc,IntPtr rect,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorProc callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr h,ref Monitor info);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);

 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)] static extern IntPtr SendText(IntPtr h,uint msg,IntPtr w,string text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)] static extern IntPtr ReadText(IntPtr h,uint msg,IntPtr w,StringBuilder text,uint flags,uint timeout,out IntPtr result);
 public static bool SetControlText(IntPtr h,string text){IntPtr result;return h!=IntPtr.Zero&&SendText(h,0x000C,IntPtr.Zero,text,2,2000,out result)!=IntPtr.Zero&&result!=IntPtr.Zero;}
 public static string ControlText(IntPtr h){var text=new StringBuilder(512);IntPtr result;if(h==IntPtr.Zero||ReadText(h,0x000D,(IntPtr)512,text,2,2000,out result)==IntPtr.Zero)return null;return text.ToString();}
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 public static Window[] Windows(int process) {var list=new List<Window>();EnumWindows((h,d)=>{uint p;GetWindowThreadProcessId(h,out p);if(process==0||p==process){var s=new StringBuilder(512);GetWindowText(h,s,512);Rect r;GetWindowRect(h,out r);list.Add(new Window{Handle=h,Title=s.ToString(),Bounds=r,Visible=IsWindowVisible(h)});}return true;},IntPtr.Zero);return list.ToArray();}
 public static Monitor[] Monitors(){var list=new List<Monitor>();EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,(h,dc,r,d)=>{var info=new Monitor();info.Size=Marshal.SizeOf(typeof(Monitor));GetMonitorInfo(h,ref info);list.Add(info);return true;},IntPtr.Zero);return list.ToArray();}
}
"@
[AetherProbe]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
if([AetherProbe]::FindWindow('Aether.Controller.v3','Aether UI Controller') -ne [IntPtr]::Zero){throw 'Close the running Aether version before this isolated validation. No existing instance was changed.'}
if($ArtifactLabel -and $ArtifactLabel -notmatch '^[a-zA-Z0-9-]{1,40}$'){throw 'ArtifactLabel must be a short alphanumeric name.'}
$run=Join-Path $root ('docs\windows-validation\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+$(if($ArtifactLabel){'-'+$ArtifactLabel}else{''}))
$profile=Join-Path $run 'profile'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
$before=[AetherProbe]::Monitors()
$native=[AetherProbe]::FindWindow('Shell_TrayWnd',$null)
$taskbarBefore=[AetherProbe]::IsWindowVisible($native)
$process=$null
function Wait-Surface([string]$title){
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 do{$window=@([AetherProbe]::Windows($process.Id) | Where-Object {$_.Title -eq $title -and $_.Visible});if($window.Count){return $window[0]};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $deadline)
 throw "Surface did not become visible: $title"
}
function Capture-Surface($window,[string]$name){
 [AetherProbe]::SetForegroundWindow($window.Handle) | Out-Null
 Start-Sleep -Milliseconds 250
 $rect=New-Object AetherProbe+Rect
 [AetherProbe]::GetWindowRect($window.Handle,[ref]$rect) | Out-Null
 $bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $run ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)}finally{$graphics.Dispose();$bitmap.Dispose()}
}
try{
 $process=Start-Process -FilePath $binary -ArgumentList @('--settings-dir',('"'+$profile+'"')) -WindowStyle Normal -PassThru
 $editor=Wait-Surface 'Aether UI - Desktop Studio'
 Start-Sleep -Seconds 3
 $windows=[AetherProbe]::Windows($process.Id)
 $tops=@($windows|Where-Object {$_.Title -eq 'Aether Top Bar' -and $_.Visible})
 $docks=@($windows|Where-Object {$_.Title -eq 'Aether Dock' -and $_.Visible})
 if($tops.Count -ne $before.Count -or $docks.Count -ne $before.Count){throw 'Panel count does not match physical monitors.'}
 if($taskbarBefore -and -not [AetherProbe]::IsWindowVisible($native)){throw 'Windows taskbar became invisible.'}
 Capture-Surface $editor 'desktop-studio'
 $dpiScale=[AetherProbe]::GetDpiForWindow($editor.Handle)/96.0
 $layoutPoint=[IntPtr](([int](175*$dpiScale) -shl 16) -bor [int](95*$dpiScale))
 [AetherProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$layoutPoint)|Out-Null
 Start-Sleep -Milliseconds 200
 Capture-Surface $editor 'desktop-studio-displays'
 $appearancePoint=[IntPtr](([int](125*$dpiScale) -shl 16) -bor [int](95*$dpiScale))
 [AetherProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$appearancePoint)|Out-Null
 foreach($dock in $docks){Capture-Surface $dock ('dock-'+$dock.Handle.ToInt64())}
 $controller=[AetherProbe]::FindWindow('Aether.Controller.v3','Aether UI Controller')
 [AetherProbe]::PostMessage($controller,0x0312,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 $activities=Wait-Surface 'Aether Activities'
 Capture-Surface $activities 'activities'
 $edit=[AetherProbe]::FindWindowEx($activities.Handle,[IntPtr]::Zero,'EDIT',$null)
 if(-not [AetherProbe]::SetControlText($edit,'terminal') -or [AetherProbe]::ControlText($edit) -ne 'terminal'){throw 'The native search EDIT did not accept the query.'}
 Start-Sleep -Milliseconds 250
 Capture-Surface $activities 'activities-search'
 $installedLaunch='Not requested'
 if($TestInstalledAppLaunch){
  $existingSettings=@([AetherProbe]::Windows(0)|Where-Object {$_.Title -eq 'Settings'})
  if($existingSettings.Count -or (Get-Process -Name SystemSettings -ErrorAction SilentlyContinue)){
   $installedLaunch='Skipped: Settings already exists; the existing app was left alone'
  }else{
   if(-not [AetherProbe]::SetControlText($edit,'Settings') -or [AetherProbe]::ControlText($edit) -ne 'Settings'){throw 'Settings query failed.'}
   Start-Sleep -Milliseconds 250
   Capture-Surface $activities 'activities-installed-launch'
   [AetherProbe]::PostMessage($edit,0x0100,[IntPtr]0x0D,[IntPtr]::Zero)|Out-Null
   $launchDeadline=[DateTime]::UtcNow.AddSeconds(15)
   do{$openedSettings=@([AetherProbe]::Windows(0)|Where-Object {$_.Title -eq 'Settings' -and $_.Visible});if($openedSettings.Count){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $launchDeadline)
   if(-not $openedSettings.Count){throw 'Selecting Settings from Activities did not open a visible Settings window.'}
   $installedLaunch='Passed: Activities opened a new visible Windows Settings window'
   foreach($window in $openedSettings){[AetherProbe]::PostMessage($window.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
   Start-Sleep -Milliseconds 750
  }
 }
 [AetherProbe]::PostMessage($activities.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 [AetherProbe]::PostMessage($editor.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 if(-not $SkipMeasurements){
  & (Join-Path $PSScriptRoot 'Measure.ps1') -Seconds $Seconds -Scenario Idle -RootProcessId $process.Id -OutputPath (Join-Path $run 'idle.json')
  if($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $run 'idle.json'))){throw 'Idle measurement failed.'}
  $idle=Get-Content -LiteralPath (Join-Path $run 'idle.json') -Raw | ConvertFrom-Json
  if(-not $idle.Samples.Count -or $idle.ActualSeconds -lt $Seconds -or $idle.Samples[-1].ProcessCount -eq 0){throw 'Idle measurement did not contain a full live-app sample.'}
  $measurement=Start-Process powershell.exe -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $PSScriptRoot 'Measure.ps1')+'"'),'-Seconds','20','-Scenario','Interaction','-RootProcessId',$process.Id,'-OutputPath',('"'+(Join-Path $run 'interaction.json')+'"')) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'interaction.log') -RedirectStandardError (Join-Path $run 'interaction-error.log')
  $clock=[Diagnostics.Stopwatch]::StartNew()
  while($clock.Elapsed.TotalSeconds -lt 20){
   [AetherProbe]::PostMessage($controller,0x0312,[IntPtr]1,[IntPtr]::Zero)|Out-Null
   Start-Sleep -Milliseconds 120
   $launcher=@([AetherProbe]::Windows($process.Id)|Where-Object {$_.Title -eq 'Aether Activities'})
   if($launcher.Count){$search=[AetherProbe]::FindWindowEx($launcher[0].Handle,[IntPtr]::Zero,'EDIT',$null);if(-not [AetherProbe]::SetControlText($search,'code')){throw 'Interaction query failed.'};Start-Sleep -Milliseconds 120;[AetherProbe]::PostMessage($search,0x0100,[IntPtr]0x28,[IntPtr]::Zero)|Out-Null;[AetherProbe]::PostMessage($launcher[0].Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}
   [AetherProbe]::PostMessage($controller,0x0312,[IntPtr]2,[IntPtr]::Zero)|Out-Null
   Start-Sleep -Milliseconds 120
   [AetherProbe]::PostMessage($editor.Handle,0x0100,[IntPtr]9,[IntPtr]::Zero)|Out-Null
   [AetherProbe]::PostMessage($editor.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
  }
  if(-not $measurement.WaitForExit(10000)){throw 'Interaction measurement did not exit.'}
  if($measurement.ExitCode -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $run 'interaction.json'))){throw 'Interaction measurement failed; inspect its logs.'}
  $interaction=Get-Content -LiteralPath (Join-Path $run 'interaction.json') -Raw | ConvertFrom-Json
  if(-not $interaction.Samples.Count -or $interaction.ActualSeconds -lt 20 -or $interaction.Samples[-1].ProcessCount -eq 0){throw 'Interaction measurement did not contain a full live-app sample.'}
 }
 [pscustomobject]@{Configuration=$Configuration;MonitorCount=$before.Count;TopBarCount=$tops.Count;DockCount=$docks.Count;TaskbarVisibleBefore=$taskbarBefore;TaskbarVisibleDuring=[AetherProbe]::IsWindowVisible($native);ProcessId=$process.Id;InstalledAppLaunch=$installedLaunch;Captures='Real on-screen app windows';Limits='No physical hotplug, DPI change, lock/unlock, game GPU or armed crash restoration tested.'} | ConvertTo-Json | Set-Content (Join-Path $run 'runtime.json')
}finally{
 if($process -and -not $process.HasExited){$owned=@([AetherProbe]::Windows($process.Id)|Where-Object {$_.Title -eq 'Aether UI Controller'});foreach($window in $owned){[AetherProbe]::PostMessage($window.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null};if(-not $process.WaitForExit(10000)){Write-Warning 'The validation process did not quit within 10 seconds; it was left running for diagnosis.'}}
 $restoreDeadline=[DateTime]::UtcNow.AddSeconds(5)
 do{$after=[AetherProbe]::Monitors();$restored=($before | ConvertTo-Json -Depth 4 -Compress) -eq ($after | ConvertTo-Json -Depth 4 -Compress);if($restored){break};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $restoreDeadline)
 [pscustomobject]@{NormalExit=($process -and $process.HasExited);ExitCode=$(if($process -and $process.HasExited){$process.ExitCode}else{$null});WorkAreasRestored=$restored;MonitorsBefore=$before;MonitorsAfter=$after} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $run 'exit.json')
 Write-Host "Validation artifacts: $run"
 if(-not $restored){throw 'Monitor work areas did not restore after normal exit.'}
 if($process -and (-not $process.HasExited -or $process.ExitCode -ne 0)){throw 'Aether did not exit normally; inspect exit.json and profile logs.'}
}