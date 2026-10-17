[CmdletBinding()]
param(
 [ValidateRange(5,3600)][int]$Seconds=60,
 [ValidateRange(1,30)][int]$Interval=1,
 [ValidateSet('General','Hotplug','MixedDpi','Session','FullscreenGame')][string]$Scenario='General',
 [string]$OutputPath=''
)
$ErrorActionPreference='Stop'
if(-not ('AetherPhysicalObserver' -as [type])){
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class AetherPhysicalObserver {
 [StructLayout(LayoutKind.Sequential)] public struct Bounds {public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct MonitorInfo {public int Size;public Bounds Bounds,Work;public uint Flags;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string Name;}
 public class MonitorRecord {public string Name;public Bounds Bounds,Work;public bool Primary;public uint? DpiX,DpiY;public int DpiResult;}
 public class SurfaceRecord {public string Handle,Title,ProcessName;public uint ProcessId,Dpi;public Bounds Bounds;public bool Visible,Minimized;}
 public class ForegroundRecord {public string Handle,Title,ClassName,ProcessName;public uint ProcessId;public bool Visible,Minimized;public Bounds? WindowBounds,FrameBounds;public string[] CoveredMonitors;}
 public class SessionRecord {public int SessionId;public int? ConnectionState;public string ConnectionStateName,InputDesktopName;public bool InputDesktopAccessible;public int InputDesktopError;}
 public class Snapshot {public MonitorRecord[] Monitors;public SurfaceRecord[] AetherSurfaces,NativeTaskbars;public ForegroundRecord Foreground;public SessionRecord Session;public bool PerMonitorObserverContext;public string[] Errors;}
 delegate bool WindowCallback(IntPtr h,IntPtr data);
 delegate bool MonitorCallback(IntPtr h,IntPtr dc,IntPtr rect,IntPtr data);
 [DllImport("user32.dll",SetLastError=true)] static extern bool EnumDisplayMonitors(IntPtr dc,IntPtr clip,MonitorCallback callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetMonitorInfo(IntPtr h,ref MonitorInfo info);
 [DllImport("shcore.dll")] static extern int GetDpiForMonitor(IntPtr h,int kind,out uint x,out uint y);
 [DllImport("user32.dll",SetLastError=true)] static extern bool EnumWindows(WindowCallback callback,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h,StringBuilder text,int count);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint process);
 [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h,out Bounds bounds);
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h,int attribute,out Bounds bounds,int bytes);
 [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
 [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetUserObjectInformation(IntPtr h,int index,StringBuilder text,uint length,out uint needed);
 [DllImport("wtsapi32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool WTSQuerySessionInformation(IntPtr server,int session,int kind,out IntPtr buffer,out uint bytes);
 [DllImport("wtsapi32.dll")] static extern void WTSFreeMemory(IntPtr buffer);
 static string Text(IntPtr h,bool cls){var text=new StringBuilder(512);if(cls)GetClassName(h,text,text.Capacity);else GetWindowText(h,text,text.Capacity);return text.ToString();}
 static string ProcessName(uint pid){try{using(var process=Process.GetProcessById((int)pid))return process.ProcessName;}catch{return null;}}
 static SurfaceRecord Surface(IntPtr h,string title,uint pid){Bounds bounds;GetWindowRect(h,out bounds);return new SurfaceRecord{Handle="0x"+h.ToInt64().ToString("X"),Title=title,ProcessId=pid,ProcessName=ProcessName(pid),Dpi=GetDpiForWindow(h),Bounds=bounds,Visible=IsWindowVisible(h),Minimized=IsIconic(h)};}
 static SessionRecord Session(){
  var result=new SessionRecord();using(var process=Process.GetCurrentProcess())result.SessionId=process.SessionId;
  IntPtr buffer;uint bytes;if(WTSQuerySessionInformation(IntPtr.Zero,result.SessionId,8,out buffer,out bytes)){
   try{if(bytes>=4){result.ConnectionState=Marshal.ReadInt32(buffer);var names=new[]{"Active","Connected","ConnectQuery","Shadow","Disconnected","Idle","Listen","Reset","Down","Init"};int state=result.ConnectionState.Value;result.ConnectionStateName=state>=0&&state<names.Length?names[state]:"Unknown";}}finally{WTSFreeMemory(buffer);}
  }
  IntPtr desktop=OpenInputDesktop(0,false,1);result.InputDesktopAccessible=desktop!=IntPtr.Zero;
  if(desktop==IntPtr.Zero)result.InputDesktopError=Marshal.GetLastWin32Error();
  else try{var name=new StringBuilder(512);uint needed;if(GetUserObjectInformation(desktop,2,name,(uint)name.Capacity*2,out needed))result.InputDesktopName=name.ToString();else result.InputDesktopError=Marshal.GetLastWin32Error();}finally{CloseDesktop(desktop);}
  return result;
 }
 public static Snapshot Capture(){
  IntPtr previous=SetThreadDpiAwarenessContext(new IntPtr(-4));
  try{
   var monitors=new List<MonitorRecord>();var surfaces=new List<SurfaceRecord>();var taskbars=new List<SurfaceRecord>();var errors=new List<string>();
   if(!EnumDisplayMonitors(IntPtr.Zero,IntPtr.Zero,(h,dc,r,d)=>{
    var info=new MonitorInfo();info.Size=Marshal.SizeOf(typeof(MonitorInfo));if(!GetMonitorInfo(h,ref info)){errors.Add("GetMonitorInfo: "+Marshal.GetLastWin32Error());return true;}
    uint x,y;int status=GetDpiForMonitor(h,0,out x,out y);monitors.Add(new MonitorRecord{Name=info.Name,Bounds=info.Bounds,Work=info.Work,Primary=(info.Flags&1)!=0,DpiX=status==0?(uint?)x:null,DpiY=status==0?(uint?)y:null,DpiResult=status});return true;
   },IntPtr.Zero))errors.Add("EnumDisplayMonitors: "+Marshal.GetLastWin32Error());
   monitors.Sort((a,b)=>StringComparer.Ordinal.Compare(a.Name,b.Name));
   var titles=new HashSet<string>{"Aether Top Bar","Aether Dock","Aether Activities","Aether UI - Desktop Studio","Aether Quick Settings","Aether Dock Windows","Aether UI Controller"};
   if(!EnumWindows((h,d)=>{
    string title=Text(h,false),cls=Text(h,true);uint pid;GetWindowThreadProcessId(h,out pid);
    if(titles.Contains(title)&&String.Equals(ProcessName(pid),"AetherUI",StringComparison.OrdinalIgnoreCase))surfaces.Add(Surface(h,title,pid));
    if(cls=="Shell_TrayWnd"||cls=="Shell_SecondaryTrayWnd")taskbars.Add(Surface(h,title,pid));return true;
   },IntPtr.Zero))errors.Add("EnumWindows: "+Marshal.GetLastWin32Error());
   surfaces.Sort((a,b)=>StringComparer.Ordinal.Compare(a.Handle,b.Handle));taskbars.Sort((a,b)=>StringComparer.Ordinal.Compare(a.Handle,b.Handle));
   ForegroundRecord foreground=null;IntPtr active=GetForegroundWindow();
   if(active!=IntPtr.Zero){uint pid;GetWindowThreadProcessId(active,out pid);Bounds window,frame;Bounds? observed=GetWindowRect(active,out window)?(Bounds?)window:null;Bounds? extended=DwmGetWindowAttribute(active,9,out frame,Marshal.SizeOf(typeof(Bounds)))==0?(Bounds?)frame:null;
    var covered=new List<string>();Bounds? candidate=extended??observed;bool visible=IsWindowVisible(active),minimized=IsIconic(active);
    if(candidate.HasValue&&visible&&!minimized){var bounds=candidate.Value;foreach(var monitor in monitors)if(bounds.Left<=monitor.Bounds.Left+3&&bounds.Top<=monitor.Bounds.Top+3&&bounds.Right>=monitor.Bounds.Right-3&&bounds.Bottom>=monitor.Bounds.Bottom-3)covered.Add(monitor.Name);}
    foreground=new ForegroundRecord{Handle="0x"+active.ToInt64().ToString("X"),Title=Text(active,false),ClassName=Text(active,true),ProcessId=pid,ProcessName=ProcessName(pid),WindowBounds=observed,FrameBounds=extended,Visible=visible,Minimized=minimized,CoveredMonitors=covered.ToArray()};
   }
   return new Snapshot{Monitors=monitors.ToArray(),AetherSurfaces=surfaces.ToArray(),NativeTaskbars=taskbars.ToArray(),Foreground=foreground,Session=Session(),PerMonitorObserverContext=previous!=IntPtr.Zero,Errors=errors.ToArray()};
  }finally{if(previous!=IntPtr.Zero)SetThreadDpiAwarenessContext(previous);}
 }
}
'@
}
if(-not $OutputPath){$OutputPath=Join-Path (Split-Path -Parent $PSScriptRoot) ('docs\physical-observations\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Scenario+'.json')}
$OutputPath=[IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force | Out-Null
$samples=New-Object 'System.Collections.Generic.List[object]'
$changes=New-Object 'System.Collections.Generic.List[object]'
$clock=[Diagnostics.Stopwatch]::StartNew()
$previous=$null
Write-Host "Observing $Scenario for $Seconds seconds. Perform physical actions yourself. Output requires manual review."
do {
 $capture=[AetherPhysicalObserver]::Capture()
 $elapsed=[Math]::Round($clock.Elapsed.TotalSeconds,3)
 $sample=[pscustomobject]@{CapturedUtc=[DateTime]::UtcNow.ToString('o');ElapsedSeconds=$elapsed;Monitors=@($capture.Monitors);AetherSurfaces=@($capture.AetherSurfaces);NativeTaskbars=@($capture.NativeTaskbars);Foreground=$capture.Foreground;Session=$capture.Session;PerMonitorObserverContext=$capture.PerMonitorObserverContext;Errors=@($capture.Errors)}
 $samples.Add($sample)
 if($previous){
  $topology=ConvertTo-Json -InputObject @($sample.Monitors|Select-Object Name,Bounds,Primary) -Depth 6 -Compress
  $oldTopology=ConvertTo-Json -InputObject @($previous.Monitors|Select-Object Name,Bounds,Primary) -Depth 6 -Compress
  $dpi=ConvertTo-Json -InputObject @($sample.Monitors|Select-Object Name,DpiX,DpiY,DpiResult) -Compress
  $oldDpi=ConvertTo-Json -InputObject @($previous.Monitors|Select-Object Name,DpiX,DpiY,DpiResult) -Compress
  $panels=ConvertTo-Json -InputObject @($sample.AetherSurfaces) -Depth 6 -Compress
  $oldPanels=ConvertTo-Json -InputObject @($previous.AetherSurfaces) -Depth 6 -Compress
  $session=ConvertTo-Json -InputObject $sample.Session -Compress
  $oldSession=ConvertTo-Json -InputObject $previous.Session -Compress
  $work=ConvertTo-Json -InputObject @($sample.Monitors|Select-Object Name,Work) -Depth 6 -Compress
  $oldWork=ConvertTo-Json -InputObject @($previous.Monitors|Select-Object Name,Work) -Depth 6 -Compress
  $foreground=ConvertTo-Json -InputObject $sample.Foreground -Depth 6 -Compress
  $oldForeground=ConvertTo-Json -InputObject $previous.Foreground -Depth 6 -Compress
  if($topology -ne $oldTopology -or $dpi -ne $oldDpi -or $panels -ne $oldPanels -or $session -ne $oldSession -or $work -ne $oldWork -or $foreground -ne $oldForeground){
   $changes.Add([pscustomobject]@{CapturedUtc=$sample.CapturedUtc;ElapsedSeconds=$elapsed;TopologyChanged=$topology -ne $oldTopology;DpiChanged=$dpi -ne $oldDpi;AetherSurfacesChanged=$panels -ne $oldPanels;SessionChanged=$session -ne $oldSession;WorkAreasChanged=$work -ne $oldWork;ForegroundChanged=$foreground -ne $oldForeground})
  }
 }
 $previous=$sample
 $remaining=$Seconds-$clock.Elapsed.TotalSeconds
 if($remaining -le 0){break}
 Start-Sleep -Milliseconds ([int][Math]::Ceiling([Math]::Min($Interval,$remaining)*1000))
}while($true)
$clock.Stop()
$result=[pscustomobject]@{
 SchemaVersion=1;CapturedUtc=[DateTime]::UtcNow.ToString('o');Scenario=$Scenario;RequestedSeconds=$Seconds;ObservedSeconds=[Math]::Round($clock.Elapsed.TotalSeconds,3);IntervalSeconds=$Interval
 Grade='requires manual review';AutomatedPass=$false
 Limitations=@('No physical actions are performed.','Unchanged topology does not establish hotplug.','Distinct DPI requires rendering and interaction review.','Input desktop access errors do not prove lock state.','Foreground monitor coverage is geometric, not proof of a fullscreen game or performance.','Sample gaps may hide short transitions.','Taskbar hiding and armed recovery remain untested.')
 Transitions=@($changes.ToArray());Samples=@($samples.ToArray())
}
$result|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $OutputPath -Encoding UTF8
Write-Host "Saved $($samples.Count) raw samples and $($changes.Count) observed transitions to $OutputPath"
Write-Host 'Grade: requires manual review'

