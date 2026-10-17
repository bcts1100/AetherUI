[CmdletBinding()]
param(
 [ValidateSet('Release','Debug')][string]$Configuration='Release',
 [string]$BinaryPath='',
 [switch]$FixtureMode,
 [string]$FixtureStatePath='',
 [int]$FixtureX=40,
 [int]$FixtureY=100
)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms,System.Drawing

# The helper owns one ordinary foreign-process native window. Its console is hidden;
# Application.Run pumps the form until the owner posts normal WM_CLOSE.
if($FixtureMode){
 if(-not $FixtureStatePath){throw 'Fixture mode requires an owned state-file path.'}
 Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @"
using System;
using System.IO;
using System.Drawing;
using System.Windows.Forms;
using System.Runtime.InteropServices;
public sealed class AetherLivePreviewFixture : Form {
 readonly string statePath;
 readonly Label content;
 [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
 AetherLivePreviewFixture(string path,int x,int y){
  statePath=path;Text="Aether live preview fixture";
  AutoScaleMode=AutoScaleMode.None;StartPosition=FormStartPosition.Manual;
  Location=new Point(x,y);ClientSize=new Size(640,360);
  FormBorderStyle=FormBorderStyle.FixedSingle;MaximizeBox=false;
  BackColor=Color.FromArgb(37,151,188);
  content=new Label{Dock=DockStyle.Fill,Text="LIVE PREVIEW\nINITIAL BLUE STATE",
   TextAlign=ContentAlignment.MiddleCenter,ForeColor=Color.White,BackColor=BackColor,
   Font=new Font("Segoe UI",30,FontStyle.Bold,GraphicsUnit.Pixel)};
  Controls.Add(content);
 }
 protected override void OnShown(EventArgs e){
  base.OnShown(e);
  // Hidden console startup can suppress the first ShowWindow call. A queued second
  // show presents only this explicitly requested fixture, without showing a console.
  BeginInvoke((MethodInvoker)(()=>{ShowWindow(Handle,4);Update();File.WriteAllText(statePath,"Initial");}));
 }
 protected override void WndProc(ref Message message){
  if(message.Msg==0x8001){
   BackColor=Color.FromArgb(194,56,105);content.BackColor=BackColor;
   content.Text="LIVE PREVIEW\nUPDATED PINK STATE";Invalidate(true);Update();
   File.WriteAllText(statePath,"Changed");return;
  }
  base.WndProc(ref message);
 }
 public static void Run(string path,int x,int y){
  SetProcessDpiAwarenessContext((IntPtr)(-4));Application.EnableVisualStyles();
  using(var form=new AetherLivePreviewFixture(path,x,y))Application.Run(form);
 }
}
"@
 [AetherLivePreviewFixture]::Run([IO.Path]::GetFullPath($FixtureStatePath),$FixtureX,$FixtureY)
 exit 0
}

$root=Split-Path -Parent $PSScriptRoot
$binary=Join-Path $root "build-v03\$Configuration\AetherUI.exe"
if($BinaryPath){$binary=[IO.Path]::GetFullPath($BinaryPath)}
$stagePrefix=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\'
if(-not [IO.Path]::GetFullPath($binary).StartsWith($stagePrefix,[StringComparison]::OrdinalIgnoreCase)){
 throw 'Preview validation only launches a binary inside the staged Aether source directory.'
}
if(-not (Test-Path -LiteralPath $binary -PathType Leaf)){throw "Staged binary missing: $binary"}
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class AetherPreviewProbe {
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct Monitor {
  public int Size;public Rect Bounds,Work;public uint Flags;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Name;
 }
 public sealed class Window {public IntPtr Handle;public string Title,Class;public Rect Bounds;public bool Visible;}
 public delegate bool EnumProc(IntPtr h,IntPtr data);
 public delegate bool MonitorProc(IntPtr h,IntPtr dc,IntPtr rect,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorProc callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern bool GetMonitorInfo(IntPtr h,ref Monitor info);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
 [DllImport("kernel32.dll",SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr process,out uint code);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect rect);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("dwmapi.dll")] public static extern int DwmFlush();
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)] static extern IntPtr SendText(IntPtr h,uint msg,IntPtr w,string text,uint flags,uint timeout,out IntPtr result);
 [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode)] static extern IntPtr ReadText(IntPtr h,uint msg,IntPtr w,StringBuilder text,uint flags,uint timeout,out IntPtr result);
 public static bool Owns(IntPtr h,int process){uint actual;return IsWindow(h)&&GetWindowThreadProcessId(h,out actual)!=0&&actual==(uint)process;}
 public static bool SetControlText(IntPtr h,string text){IntPtr result;return h!=IntPtr.Zero&&SendText(h,0x000C,IntPtr.Zero,text,2,2000,out result)!=IntPtr.Zero&&result!=IntPtr.Zero;}
 public static string ControlText(IntPtr h){var text=new StringBuilder(512);IntPtr result;if(h==IntPtr.Zero||ReadText(h,0x000D,(IntPtr)512,text,2,2000,out result)==IntPtr.Zero)return null;return text.ToString();}
 public static Window[] Windows(int process){
  var list=new List<Window>();EnumWindows((h,data)=>{uint pid;GetWindowThreadProcessId(h,out pid);
   if(process==0||pid==(uint)process){var text=new StringBuilder(512);var cls=new StringBuilder(256);
    GetWindowText(h,text,512);GetClassName(h,cls,256);Rect rect;GetWindowRect(h,out rect);
    list.Add(new Window{Handle=h,Title=text.ToString(),Class=cls.ToString(),Bounds=rect,Visible=IsWindowVisible(h)});
   }return true;},IntPtr.Zero);return list.ToArray();
 }
 public static Monitor[] Monitors(){
  var list=new List<Monitor>();EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,(h,dc,rect,data)=>{
   var info=new Monitor();info.Size=Marshal.SizeOf(typeof(Monitor));GetMonitorInfo(h,ref info);list.Add(info);return true;
  },IntPtr.Zero);return list.ToArray();
 }
 public static Rect Capture(IntPtr window,string path){
  Rect rect;if(!GetWindowRect(window,out rect)||rect.Right<=rect.Left||rect.Bottom<=rect.Top)throw new InvalidOperationException("Capture window has no drawable bounds.");
  DwmFlush();using(var bitmap=new Bitmap(rect.Right-rect.Left,rect.Bottom-rect.Top)){
   using(var graphics=Graphics.FromImage(bitmap))graphics.CopyFromScreen(rect.Left,rect.Top,0,0,bitmap.Size,CopyPixelOperation.SourceCopy);
   bitmap.Save(path,ImageFormat.Png);
  }return rect;
 }
 public static double ColorRatio(string path,Rect region,int red,int green,int blue){
  using(var bitmap=new Bitmap(path)){
   int left=Math.Max(0,region.Left),top=Math.Max(0,region.Top),right=Math.Min(bitmap.Width,region.Right),bottom=Math.Min(bitmap.Height,region.Bottom);
   int matched=0,total=0;for(int y=top;y<bottom;y+=3)for(int x=left;x<right;x+=3){
    var color=bitmap.GetPixel(x,y);++total;
    if(Math.Abs(color.R-red)<=24&&Math.Abs(color.G-green)<=24&&Math.Abs(color.B-blue)<=24)++matched;
   }return total==0?0:(double)matched/total;
  }
 }
}
"@
[AetherPreviewProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))|Out-Null
if([AetherPreviewProbe]::FindWindow('Aether.Controller.v3','Aether UI Controller') -ne [IntPtr]::Zero){
 throw 'Close the current Aether instance before isolated preview validation. It was left unchanged.'
}
$run=Join-Path $root ('docs\windows-validation\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-previews')
$profile=Join-Path $run 'profile'
New-Item -ItemType Directory -Path $profile -Force|Out-Null
$statePath=Join-Path $run 'fixture-state.txt'
$before=[AetherPreviewProbe]::Monitors()
$taskbars=@([AetherPreviewProbe]::Windows(0)|Where-Object {$_.Class -eq 'Shell_TrayWnd' -or $_.Class -eq 'Shell_SecondaryTrayWnd'})
$primaryTaskbar=@($taskbars|Where-Object {$_.Class -eq 'Shell_TrayWnd'})
if(-not $before.Count -or -not $primaryTaskbar.Count -or -not $primaryTaskbar[0].Visible){throw 'This probe requires a visible native taskbar and an interactive desktop.'}
$appProcess=$null
$fixtureProcess=$null
$sessions=New-Object 'System.Collections.Generic.List[object]'
$checks=[ordered]@{}
$failure=$null
$cleanupFailures=New-Object 'System.Collections.Generic.List[string]'

function Wait-OwnWindow($ownedProcess,[string]$title,[bool]$requireVisible=$true){
 $deadline=[DateTime]::UtcNow.AddSeconds(15)
 do{
  $ownedProcess.Refresh();if($ownedProcess.HasExited){throw "Owned process exited before its window appeared: $title"}
  $windows=@([AetherPreviewProbe]::Windows($ownedProcess.Id)|Where-Object {$_.Title -eq $title -and (-not $requireVisible -or $_.Visible)})
  if($windows.Count){return $windows[0]}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $deadline)
 throw "Owned window did not appear: $title"
}
function Assert-Taskbars{
 foreach($taskbar in $taskbars){if($taskbar.Visible -and -not [AetherPreviewProbe]::IsWindowVisible($taskbar.Handle)){throw 'A native Windows taskbar became hidden during preview validation.'}}
}
function Write-Profile([bool]$enabled){
 $value=if($enabled){'1'}else{'0'}
 $contents="[AetherSettings]`nversion=3`n[behavior]`nhideTaskbar=0`nwindowPreviews=$value`n"
 [IO.File]::WriteAllText((Join-Path $profile 'settings-v3.ini'),$contents,(New-Object Text.UTF8Encoding $false))
}
function Start-OwnedAether([bool]$enabled){
 Write-Profile $enabled
 $script:appProcess=Start-Process -FilePath $binary -ArgumentList @('--settings-dir',('"'+$profile+'"')) -WindowStyle Normal -PassThru
 $null=$appProcess.Handle
 $controller=Wait-OwnWindow $appProcess 'Aether UI Controller' $false
 Assert-Taskbars
 return $controller
}
function Owned-ExitCode($ownedProcess){
 if(-not $ownedProcess.WaitForExit(1000)){throw 'Owned process is still active while checking its exit code.'}
 $exitCode=[uint32]0
 if(-not [AetherPreviewProbe]::GetExitCodeProcess($ownedProcess.Handle,[ref]$exitCode) -or $exitCode -eq 259){throw 'Native owned-process exit code is unavailable.'}
 return [int]$exitCode
}
function Wait-WorkAreasRestored{
 $baseline=$before|ConvertTo-Json -Depth 4 -Compress
 $deadline=[DateTime]::UtcNow.AddSeconds(5)
 do{
  $latest=[AetherPreviewProbe]::Monitors()
  $restored=$baseline -eq ($latest|ConvertTo-Json -Depth 4 -Compress)
  if($restored){return [pscustomobject]@{Restored=$true;Monitors=$latest}}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $deadline)
 return [pscustomobject]@{Restored=$false;Monitors=$latest}
}
function Stop-OwnedAether{
 if(-not $appProcess){return}
 $appProcess.Refresh()
 if(-not $appProcess.HasExited){
  $dialogs=@([AetherPreviewProbe]::Windows($appProcess.Id)|Where-Object {$_.Title -in @('Save palette','Rename palette','Save layout','Rename layout') -and $_.Visible})
  foreach($dialog in $dialogs){if([AetherPreviewProbe]::Owns($dialog.Handle,$appProcess.Id)){[AetherPreviewProbe]::PostMessage($dialog.Handle,0x0111,[IntPtr]2,[IntPtr]::Zero)|Out-Null}}
  if($dialogs.Count){Start-Sleep -Milliseconds 100}
  $controllers=@([AetherPreviewProbe]::Windows($appProcess.Id)|Where-Object {$_.Title -eq 'Aether UI Controller'})
  foreach($controller in $controllers){if([AetherPreviewProbe]::Owns($controller.Handle,$appProcess.Id)){[AetherPreviewProbe]::PostMessage($controller.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}}
  if(-not $appProcess.WaitForExit(15000)){throw 'Owned Aether did not exit within 15 seconds; it was left running for diagnosis.'}
 }
 $appExitCode=Owned-ExitCode $appProcess
 $restoration=Wait-WorkAreasRestored
 $afterSession=$restoration.Monitors;$restored=$restoration.Restored
 $sessions.Add([pscustomobject]@{ProcessId=$appProcess.Id;ExitCode=$appExitCode;WorkAreasRestored=$restored;TaskbarsVisible=(@($taskbars|Where-Object {$_.Visible -and -not [AetherPreviewProbe]::IsWindowVisible($_.Handle)}).Count -eq 0)})
 if($appExitCode -ne 0){throw 'Owned Aether did not exit normally.'}
 if(-not $restored){throw 'Aether did not restore monitor work areas after normal exit.'}
 Assert-Taskbars
 $script:appProcess=$null
}
function Open-FixturePreview($controller){
 if(-not [AetherPreviewProbe]::Owns($controller.Handle,$appProcess.Id)){throw 'Controller HWND/PID ownership changed.'}
 [AetherPreviewProbe]::PostMessage($controller.Handle,0x0312,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 $activities=Wait-OwnWindow $appProcess 'Aether Activities'
 $scale=[AetherPreviewProbe]::GetDpiForWindow($activities.Handle)/96.0
 $tabPoint=[IntPtr](([int](120*$scale) -shl 16) -bor [int](240*$scale))
 [AetherPreviewProbe]::PostMessage($activities.Handle,0x0202,[IntPtr]::Zero,$tabPoint)|Out-Null
 $edit=[AetherPreviewProbe]::FindWindowEx($activities.Handle,[IntPtr]::Zero,'EDIT',$null)
 if(-not [AetherPreviewProbe]::SetControlText($edit,'Aether live preview fixture') -or [AetherPreviewProbe]::ControlText($edit) -ne 'Aether live preview fixture'){
  throw 'Activities did not accept the fixture search query.'
 }
 Start-Sleep -Milliseconds 600
 return $activities
}
function Preview-Crop($activities){
 $client=New-Object AetherPreviewProbe+Rect
 if(-not [AetherPreviewProbe]::GetClientRect($activities.Handle,[ref]$client)){throw 'Activities client bounds unavailable.'}
 $scale=[AetherPreviewProbe]::GetDpiForWindow($activities.Handle)/96.0
 $width=$client.Right/$scale;$height=$client.Bottom/$scale
 if($width -lt 640 -or $height -lt 430){throw 'This visual probe requires a complete regular overview grid.'}
 $columns=[Math]::Max(1,[Math]::Min(4,[Math]::Floor(($width-28)/212)))
 $rows=[Math]::Max(1,[Math]::Min(3,[Math]::Floor(($height-174)/172)))
 $tileWidth=($width-40-12*($columns-1))/$columns
 $tileHeight=($height-186-12*($rows-1))/$rows
 # Crop the first grid tile image, excluding title, icon and selected border.
 $crop=New-Object AetherPreviewProbe+Rect
 $crop.Left=[int][Math]::Round(34*$scale);$crop.Top=[int][Math]::Round(160*$scale)
 $crop.Right=[int][Math]::Round((20+$tileWidth-14)*$scale)
 $crop.Bottom=[int][Math]::Round((146+$tileHeight-60)*$scale)
 return $crop
}
function Move-FixtureOutside($activities,$fixture){
 $launcher=New-Object AetherPreviewProbe+Rect
 $source=New-Object AetherPreviewProbe+Rect
 [AetherPreviewProbe]::GetWindowRect($activities.Handle,[ref]$launcher)|Out-Null
 [AetherPreviewProbe]::GetWindowRect($fixture.Handle,[ref]$source)|Out-Null
 $width=$source.Right-$source.Left;$height=$source.Bottom-$source.Top
 foreach($monitor in [AetherPreviewProbe]::Monitors()){
  $work=$monitor.Work
  $points=@(@(($work.Left+24),($work.Top+40)),@(($work.Right-$width-24),($work.Top+40)),@(($work.Left+24),($work.Bottom-$height-24)),@(($work.Right-$width-24),($work.Bottom-$height-24)))
  foreach($point in $points){
   $x=[int]$point[0];$y=[int]$point[1]
   $fits=$x -ge $work.Left -and $y -ge $work.Top -and $x+$width -le $work.Right -and $y+$height -le $work.Bottom
   $separate=$x+$width -le $launcher.Left-12 -or $x -ge $launcher.Right+12 -or $y+$height -le $launcher.Top-12 -or $y -ge $launcher.Bottom+12
   if($fits -and $separate){
    if(-not [AetherPreviewProbe]::Owns($fixture.Handle,$fixtureProcess.Id)){throw 'Fixture HWND/PID ownership changed.'}
    if(-not [AetherPreviewProbe]::SetWindowPos($fixture.Handle,[IntPtr]::Zero,$x,$y,$width,$height,0x14)){throw 'Could not position the owned fixture separately from Activities.'}
    Start-Sleep -Milliseconds 250
    return
   }
  }
 }
 throw 'No non-overlapping screen area can show the fixture and Activities together.'
}
function Capture-Window($window,[string]$name,[switch]$EnsureForeground){
 if(-not [AetherPreviewProbe]::IsWindowVisible($window.Handle)){throw "Capture window became hidden: $name"}
 if($EnsureForeground){
  $deadline=[DateTime]::UtcNow.AddSeconds(5)
  do{
   [AetherPreviewProbe]::SetForegroundWindow($window.Handle)|Out-Null
   Start-Sleep -Milliseconds 100
   if([AetherPreviewProbe]::GetForegroundWindow() -eq $window.Handle){break}
  }while([DateTime]::UtcNow -lt $deadline)
  if([AetherPreviewProbe]::GetForegroundWindow() -ne $window.Handle){throw "Owned window could not be brought forward for a useful capture: $name"}
  Start-Sleep -Milliseconds 150
  if([AetherPreviewProbe]::GetForegroundWindow() -ne $window.Handle){throw "Owned capture was covered by another foreground window: $name"}
 }
 $path=Join-Path $run ($name+'.png')
 [AetherPreviewProbe]::Capture($window.Handle,$path)|Out-Null
 return $path
}
function Submit-PresetName([string]$title,[string]$name,[string]$captureName){
 $dialog=Wait-OwnWindow $appProcess $title
 $edit=[AetherPreviewProbe]::GetDlgItem($dialog.Handle,101)
 if($edit -eq [IntPtr]::Zero -or -not [AetherPreviewProbe]::SetControlText($edit,$name) -or [AetherPreviewProbe]::ControlText($edit) -ne $name){throw "Native preset dialog did not accept its name: $title"}
 Start-Sleep -Milliseconds 150
 $capture=Capture-Window $dialog $captureName -EnsureForeground
 [AetherPreviewProbe]::PostMessage($dialog.Handle,0x0111,[IntPtr]1,[IntPtr]::Zero)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(5)
 do{if(-not [AetherPreviewProbe]::IsWindowVisible($dialog.Handle)){return $capture};Start-Sleep -Milliseconds 50}while([DateTime]::UtcNow -lt $deadline)
 throw "Native preset dialog did not finish saving: $title"
}
function Wait-PresetFile([string]$directory,[string]$pattern){
 $deadline=[DateTime]::UtcNow.AddSeconds(5)
 do{
  $presets=@(Get-ChildItem -LiteralPath $directory -Filter $pattern -File -ErrorAction SilentlyContinue)
  if($presets.Count -eq 1){$presets[0].Refresh();if($presets[0].Length -gt 0){return $presets[0]}}
  Start-Sleep -Milliseconds 100
 }while([DateTime]::UtcNow -lt $deadline)
 throw "Native save action did not create exactly one nonempty in-profile preset: $pattern"
}
function Capture-SavedPalette($controller){
 [AetherPreviewProbe]::PostMessage($controller.Handle,0x0312,[IntPtr]2,[IntPtr]::Zero)|Out-Null
 $editor=Wait-OwnWindow $appProcess 'Aether UI - Desktop Studio'
 $client=New-Object AetherPreviewProbe+Rect
 [AetherPreviewProbe]::GetClientRect($editor.Handle,[ref]$client)|Out-Null
 $dpiScale=[AetherPreviewProbe]::GetDpiForWindow($editor.Handle)/96.0
 $layoutScale=[Math]::Min(1.0,[Math]::Max(.1,$client.Right/$dpiScale/730.0))
 $effectiveScale=$dpiScale*$layoutScale
 $appearance=[IntPtr](([int](125*$effectiveScale) -shl 16) -bor [int](95*$effectiveScale))
 [AetherPreviewProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$appearance)|Out-Null
 Start-Sleep -Milliseconds 200
 for($page=0;$page -lt 3;$page++){[AetherPreviewProbe]::PostMessage($editor.Handle,0x0100,[IntPtr]0x22,[IntPtr]::Zero)|Out-Null}
 Start-Sleep -Milliseconds 300
 $maxScroll=[Math]::Max(0.0,654.0-($client.Bottom/$effectiveScale-151.0))
 $scroll=[Math]::Min(495.0,$maxScroll)
 $saveY=114.0-$scroll+510.0+18.0
 if($saveY -lt 108 -or $saveY+19 -ge $client.Bottom/$effectiveScale-40){throw 'Saved-palette action is not fully visible in this editor layout.'}
 $savePoint=[IntPtr](([int]($saveY*$effectiveScale) -shl 16) -bor [int](325*$effectiveScale))
 [AetherPreviewProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$savePoint)|Out-Null
 $nameCapture=Submit-PresetName 'Save palette' 'Evening workspace' 'desktop-studio-name-palette'
 $preset=Wait-PresetFile (Join-Path $profile 'theme-presets') 'preset-*.aethertheme'
 Start-Sleep -Milliseconds 300
 $capture=Capture-Window $editor 'desktop-studio-saved-palette' -EnsureForeground
 $checks.SavedPalette=[pscustomobject]@{Name='Evening workspace';File=$preset.FullName;Bytes=$preset.Length;Capture=$capture;NameDialogCapture=$nameCapture;SavedThroughNativeEditor=$true}
 $layoutPoint=[IntPtr](([int](175*$effectiveScale) -shl 16) -bor [int](95*$effectiveScale))
 [AetherPreviewProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$layoutPoint)|Out-Null
 Start-Sleep -Milliseconds 200
 for($page=0;$page -lt 3;$page++){[AetherPreviewProbe]::PostMessage($editor.Handle,0x0100,[IntPtr]0x22,[IntPtr]::Zero)|Out-Null}
 Start-Sleep -Milliseconds 300
 $maxScroll=[Math]::Max(0.0,1050.0-($client.Bottom/$effectiveScale-151.0))
 $scroll=[Math]::Min(495.0,$maxScroll)
 $saveY=114.0+144.0+685.0+18.0-$scroll
 if($saveY -lt 108 -or $saveY+19 -ge $client.Bottom/$effectiveScale-40){throw 'Saved-layout action is not fully visible in this editor layout.'}
 $savePoint=[IntPtr](([int]($saveY*$effectiveScale) -shl 16) -bor [int](325*$effectiveScale))
 [AetherPreviewProbe]::PostMessage($editor.Handle,0x0202,[IntPtr]::Zero,$savePoint)|Out-Null
 $nameCapture=Submit-PresetName 'Save layout' 'Studio workspace' 'desktop-studio-name-layout'
 $preset=Wait-PresetFile (Join-Path $profile 'layout-presets') 'preset-*.aetherlayout'
 for($page=0;$page -lt 4;$page++){[AetherPreviewProbe]::PostMessage($editor.Handle,0x0100,[IntPtr]0x22,[IntPtr]::Zero)|Out-Null}
 Start-Sleep -Milliseconds 300
 $capture=Capture-Window $editor 'desktop-studio-saved-layout' -EnsureForeground
 $checks.SavedLayout=[pscustomobject]@{Name='Studio workspace';File=$preset.FullName;Bytes=$preset.Length;Capture=$capture;NameDialogCapture=$nameCapture;SavedThroughNativeEditor=$true}
 [AetherPreviewProbe]::PostMessage($editor.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 200
 if([AetherPreviewProbe]::IsWindowVisible($editor.Handle)){throw 'Desktop Studio did not hide after normal close.'}
}
function Whole-WindowCrop([string]$path){
 $bitmap=New-Object Drawing.Bitmap $path
 try{$crop=New-Object AetherPreviewProbe+Rect;$crop.Right=$bitmap.Width;$crop.Bottom=$bitmap.Height;return $crop}finally{$bitmap.Dispose()}
}
try{
 $powerShell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
 $initial=$before[0].Work
 $fixtureProcess=Start-Process -FilePath $powerShell -ArgumentList @('-NoProfile','-STA','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-FixtureMode','-FixtureStatePath',('"'+$statePath+'"'),'-FixtureX',($initial.Left+40),'-FixtureY',($initial.Top+100)) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'fixture-output.log') -RedirectStandardError (Join-Path $run 'fixture-error.log')
 $null=$fixtureProcess.Handle
 $fixture=Wait-OwnWindow $fixtureProcess 'Aether live preview fixture'
 [AetherPreviewProbe]::SetForegroundWindow($fixture.Handle)|Out-Null
 $controller=Start-OwnedAether $true
 $activities=Open-FixturePreview $controller
 Move-FixtureOutside $activities $fixture
 $crop=Preview-Crop $activities
 $sourceInitial=Capture-Window $fixture 'fixture-initial'
 $previewInitial=Capture-Window $activities 'activities-preview-initial'
 $initialSourceRatio=[AetherPreviewProbe]::ColorRatio($sourceInitial,(Whole-WindowCrop $sourceInitial),37,151,188)
 $initialRatio=[AetherPreviewProbe]::ColorRatio($previewInitial,$crop,37,151,188)
 if($initialSourceRatio -lt .40 -or $initialRatio -lt .08){throw "Initial fixture was not visibly useful in Activities (source=$initialSourceRatio preview=$initialRatio)."}
 # Mutation targets only the fixture and retains its title. No app messages, selection
 # changes, re-registration request, or focus switch occurs before the live capture.
 if(-not [AetherPreviewProbe]::Owns($fixture.Handle,$fixtureProcess.Id)){throw 'Fixture ownership changed before mutation.'}
 [AetherPreviewProbe]::PostMessage($fixture.Handle,0x8001,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 $deadline=[DateTime]::UtcNow.AddSeconds(5)
 do{if((Test-Path -LiteralPath $statePath) -and (Get-Content -LiteralPath $statePath -Raw) -eq 'Changed'){break};Start-Sleep -Milliseconds 100}while([DateTime]::UtcNow -lt $deadline)
 if(-not (Test-Path -LiteralPath $statePath) -or (Get-Content -LiteralPath $statePath -Raw) -ne 'Changed'){throw 'Owned fixture did not acknowledge live content mutation.'}
 Start-Sleep -Milliseconds 750
 $sourceChanged=Capture-Window $fixture 'fixture-updated'
 $previewChanged=Capture-Window $activities 'activities-preview-updated'
 $changedSourceRatio=[AetherPreviewProbe]::ColorRatio($sourceChanged,(Whole-WindowCrop $sourceChanged),194,56,105)
 $changedRatio=[AetherPreviewProbe]::ColorRatio($previewChanged,$crop,194,56,105)
 $oldRatioAfterMutation=[AetherPreviewProbe]::ColorRatio($previewChanged,$crop,37,151,188)
 if($changedSourceRatio -lt .40 -or $changedRatio -lt .08 -or $oldRatioAfterMutation -gt $initialRatio*.25){throw 'The integrated Activities thumbnail did not show the live color update.'}
 $checks.LiveUpdate=[pscustomobject]@{InitialSourceColorRatio=$initialSourceRatio;InitialPreviewColorRatio=$initialRatio;UpdatedSourceColorRatio=$changedSourceRatio;UpdatedPreviewColorRatio=$changedRatio;OldColorAfterMutation=$oldRatioAfterMutation;Region=$crop;WindowTitleUnchanged=([AetherPreviewProbe]::Windows($fixtureProcess.Id)|Where-Object {$_.Handle -eq $fixture.Handle}).Title -eq 'Aether live preview fixture'}
 $scale=[AetherPreviewProbe]::GetDpiForWindow($activities.Handle)/96.0
 $appsPoint=[IntPtr](([int](120*$scale) -shl 16) -bor [int](95*$scale))
 [AetherPreviewProbe]::PostMessage($activities.Handle,0x0202,[IntPtr]::Zero,$appsPoint)|Out-Null
 Start-Sleep -Milliseconds 300
 $applications=Capture-Window $activities 'activities-applications-no-preview'
 $tabRatio=[AetherPreviewProbe]::ColorRatio($applications,$crop,194,56,105)
 if($tabRatio -gt .02){throw 'Switching to Applications left fixture content in the preview region.'}
 $checks.TabRelease=[pscustomobject]@{FixtureColorRatioAfterTabSwitch=$tabRatio}
 [AetherPreviewProbe]::PostMessage($activities.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null
 Start-Sleep -Milliseconds 200
 if([AetherPreviewProbe]::IsWindowVisible($activities.Handle)){throw 'Activities did not hide after normal close.'}
 $checks.HiddenAfterClose=$true
 Assert-Taskbars
 Stop-OwnedAether
 $controller=Start-OwnedAether $false
 $disabledActivities=Open-FixturePreview $controller
 $disabled=Capture-Window $disabledActivities 'activities-previews-disabled'
 $disabledRatio=[AetherPreviewProbe]::ColorRatio($disabled,(Preview-Crop $disabledActivities),194,56,105)
 if($disabledRatio -gt .02){throw 'Saved windowPreviews=0 still rendered fixture content.'}
 $checks.DisabledPreference=[pscustomobject]@{SavedWindowPreviews=$false;FixtureColorRatio=$disabledRatio}
 Capture-SavedPalette $controller
 Assert-Taskbars
 Stop-OwnedAether
}catch{
 $failure=$_
}finally{
 try{Stop-OwnedAether}catch{$cleanupFailures.Add($_.Exception.Message)}
 if($fixtureProcess){
  $fixtureProcess.Refresh()
  if(-not $fixtureProcess.HasExited){
   $owned=@([AetherPreviewProbe]::Windows($fixtureProcess.Id)|Where-Object {$_.Title -eq 'Aether live preview fixture'})
   foreach($window in $owned){if([AetherPreviewProbe]::Owns($window.Handle,$fixtureProcess.Id)){[AetherPreviewProbe]::PostMessage($window.Handle,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)|Out-Null}}
   if(-not $fixtureProcess.WaitForExit(10000)){$cleanupFailures.Add('Owned fixture did not close normally; it was left running for diagnosis.')}
  }
  if($fixtureProcess.HasExited){
   try{$fixtureExitCode=Owned-ExitCode $fixtureProcess;if($fixtureExitCode -ne 0){$cleanupFailures.Add('Owned fixture returned a nonzero exit code; inspect fixture-error.log.')}}catch{$cleanupFailures.Add($_.Exception.Message)}
  }
 }
 $restoration=Wait-WorkAreasRestored
 $after=$restoration.Monitors;$restored=$restoration.Restored
 $taskbarsVisible=@($taskbars|Where-Object {$_.Visible -and -not [AetherPreviewProbe]::IsWindowVisible($_.Handle)}).Count -eq 0
 if(-not $restored){$cleanupFailures.Add('Final monitor work areas differ from the pre-probe snapshot.')}
 if(-not $taskbarsVisible){$cleanupFailures.Add('A native taskbar is hidden after the probe.')}
 [pscustomobject]@{Configuration=$Configuration;Binary=$binary;Checks=$checks;Success=($null -eq $failure -and $cleanupFailures.Count -eq 0);Failure=$(if($failure){$failure.Exception.Message}else{$null});Captures='Actual on-screen native fixture and Activities windows';Limits='Regular overview grid with one real foreign-process live tile. Pixel ratios establish distinctive fixture content and its update; inspect PNGs for full visual quality. Native ownership tests separately establish unregister behavior.'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $run 'runtime.json') -Encoding utf8
 [pscustomobject]@{Sessions=$sessions.ToArray();FixtureProcessId=$(if($fixtureProcess){$fixtureProcess.Id}else{$null});FixtureExitCode=$fixtureExitCode;FixtureNormalExit=($fixtureProcess -and $fixtureProcess.HasExited -and $fixtureExitCode -eq 0);WorkAreasRestored=$restored;TaskbarsVisible=$taskbarsVisible;CleanupFailures=$cleanupFailures.ToArray();MonitorsBefore=$before;MonitorsAfter=$after}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $run 'exit.json') -Encoding utf8
 Write-Host "Preview validation artifacts: $run"
}
if($failure){throw $failure}
if($cleanupFailures.Count){throw ($cleanupFailures -join ' ')}
Write-Host 'Integrated live preview, tab cleanup, disabled preference and normal owned-process exits passed.'







