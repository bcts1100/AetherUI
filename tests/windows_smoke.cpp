#include "platform/windows.hpp"
#include "platform/desktop.hpp"
#include "platform/display_policy.hpp"
#include "platform/config.hpp"
#include "platform/safety.hpp"
#include <iostream>
#include <cstdint>
namespace {
struct NativeBar {HWND hwnd{};DWORD pid{};bool visible{};};
std::vector<NativeBar> nativeBars(){
 std::vector<NativeBar> bars;
 EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
  wchar_t name[64]{};GetClassNameW(hwnd,name,64);
  if(wcscmp(name,L"Shell_TrayWnd")&&wcscmp(name,L"Shell_SecondaryTrayWnd"))return TRUE;
  DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
  reinterpret_cast<std::vector<NativeBar>*>(data)->push_back({hwnd,pid,IsWindowVisible(hwnd)!=FALSE});
  return TRUE;
 },reinterpret_cast<LPARAM>(&bars));
 return bars;
}
constexpr wchar_t OcclusionClass[]=L"LockScreenInputOcclusionFrame";
struct NativeOcclusionFrame {
 HWND hwnd{};DWORD pid{};ULONGLONG birth{};aether::win::Handle process;std::wstring path;
};
ULONGLONG birthOf(HANDLE process){
 FILETIME created{},exited{},kernel{},user{};
 if(!process||!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
 return (static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
std::wstring imageOf(HANDLE process){
 wchar_t image[32768]{};DWORD count=32768;
 return process&&QueryFullProcessImageNameW(process,0,image,&count)?std::wstring(image,count):std::wstring{};
}
std::wstring explorerImage(){
 wchar_t directory[32768]{};const UINT count=GetWindowsDirectoryW(directory,32768);
 return count&&count<32768?(std::filesystem::path(directory)/L"explorer.exe").wstring():std::wstring{};
}
bool genuineOcclusionFrame(const NativeOcclusionFrame& frame,const std::wstring& expected){
 if(expected.empty()||!frame.process||!frame.birth||GetProcessId(frame.process.value)!=frame.pid
  ||WaitForSingleObject(frame.process.value,0)!=WAIT_TIMEOUT||birthOf(frame.process.value)!=frame.birth)return false;
 DWORD pid=0;GetWindowThreadProcessId(frame.hwnd,&pid);wchar_t name[128]{};
 return pid==frame.pid&&GetClassNameW(frame.hwnd,name,128)&&wcscmp(name,OcclusionClass)==0
  &&_wcsicmp(frame.path.c_str(),expected.c_str())==0&&_wcsicmp(imageOf(frame.process.value).c_str(),expected.c_str())==0;
}
std::vector<NativeOcclusionFrame> nativeOcclusionFrames(const std::wstring& expected,bool& complete){
 struct Search {const std::wstring* expected;std::vector<NativeOcclusionFrame> frames;} search{&expected,{}};
 complete=EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
  auto& find=*reinterpret_cast<Search*>(data);wchar_t name[128]{};
  if(!GetClassNameW(hwnd,name,128)||wcscmp(name,OcclusionClass)!=0)return TRUE;
  NativeOcclusionFrame frame;frame.hwnd=hwnd;GetWindowThreadProcessId(hwnd,&frame.pid);
  if(!frame.pid)return TRUE;
  frame.process=aether::win::Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,frame.pid));
  frame.birth=birthOf(frame.process.value);frame.path=imageOf(frame.process.value);
  if(genuineOcclusionFrame(frame,*find.expected))find.frames.push_back(std::move(frame));
  return TRUE;
 },reinterpret_cast<LPARAM>(&search))!=FALSE;
 return std::move(search.frames);
}
bool occlusionExcluded(const std::vector<NativeOcclusionFrame>& frames,const std::wstring& expected,
 const std::vector<aether::win::Window>& windows,bool complete){
 std::size_t verified=0;bool excluded=true;
 for(const auto& frame:frames){
  if(!genuineOcclusionFrame(frame,expected)){
   std::cout<<"DIAGNOSTIC genuine occlusion frame changed before observation; conditional coverage unavailable for hwnd="<<reinterpret_cast<std::uintptr_t>(frame.hwnd)<<'\n';continue;
  }
  const auto found=std::find_if(windows.begin(),windows.end(),[&](const auto& window){return window.hwnd==frame.hwnd&&window.pid==frame.pid;});
  RECT rect{};const bool rectKnown=GetWindowRect(frame.hwnd,&rect)!=FALSE;DWORD cloak=0;
  const HRESULT cloakResult=DwmGetWindowAttribute(frame.hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak));
  std::cout<<"DIAGNOSTIC genuine Explorer occlusion frame hwnd="<<reinterpret_cast<std::uintptr_t>(frame.hwnd)
   <<" pid="<<frame.pid<<" birth="<<frame.birth<<" class="<<aether::win::utf8(OcclusionClass)
   <<" title="<<aether::win::utf8(aether::win::windowText(frame.hwnd))
   <<" visible="<<(IsWindowVisible(frame.hwnd)!=FALSE)<<" style="<<GetWindowLongPtrW(frame.hwnd,GWL_STYLE)
   <<" exStyle="<<GetWindowLongPtrW(frame.hwnd,GWL_EXSTYLE)<<" cloakKnown="<<SUCCEEDED(cloakResult)
   <<" cloak="<<cloak<<" cloakResult="<<cloakResult<<" boundsKnown="<<rectKnown
   <<" bounds="<<rect.left<<","<<rect.top<<","<<rect.right<<","<<rect.bottom
   <<" path="<<aether::win::utf8(frame.path)<<" collectorIncludes="<<(found!=windows.end());
  if(found!=windows.end())std::cout<<" collectedFullscreen="<<found->fullscreen<<" collectedDisplay="<<aether::win::utf8(found->display);
  std::cout<<'\n';
  // Keep the held process identity through the collector and metadata reads.
  // A vanished/reused HWND is not evidence against the collector.
  if(!genuineOcclusionFrame(frame,expected)){
   std::cout<<"DIAGNOSTIC occlusion identity changed during observation; conditional coverage unavailable for this frame.\n";continue;
  }
  ++verified;
  if(found!=windows.end()){excluded=false;std::cerr<<"FAIL genuine Explorer occlusion frame was included in WindowCollector.\n";}
 }
 if(!complete)std::cout<<"DIAGNOSTIC occlusion enumeration incomplete; exhaustive conditional coverage unavailable.\n";
 if(!verified)std::cout<<"DIAGNOSTIC no stable genuine Explorer LockScreenInputOcclusionFrame observed; conditional coverage unavailable.\n";
 else if(excluded)std::cout<<"PASS "<<verified<<" independently verified genuine Explorer occlusion frame(s) excluded from WindowCollector.\n";
 return excluded;
}
struct NativeDecoratedMaximized {
 HWND hwnd{};DWORD pid{};ULONGLONG birth{};aether::win::Handle process;std::wstring path,className;
 LONG_PTR style{},exStyle{};RECT nativeBounds{},frameBounds{};
};
bool decoratedMaximizedStable(const NativeDecoratedMaximized& window){
 if(!window.process||!window.birth||GetProcessId(window.process.value)!=window.pid
  ||WaitForSingleObject(window.process.value,0)!=WAIT_TIMEOUT||birthOf(window.process.value)!=window.birth)return false;
 DWORD pid=0;GetWindowThreadProcessId(window.hwnd,&pid);wchar_t name[256]{};RECT native{},frame{};
 if(pid!=window.pid||!GetClassNameW(window.hwnd,name,256)||window.className!=name
  ||!IsWindowVisible(window.hwnd)||!IsZoomed(window.hwnd)||IsIconic(window.hwnd)
  ||GetWindowLongPtrW(window.hwnd,GWL_STYLE)!=window.style||GetWindowLongPtrW(window.hwnd,GWL_EXSTYLE)!=window.exStyle
  ||!GetWindowRect(window.hwnd,&native)||FAILED(DwmGetWindowAttribute(window.hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame))))return false;
 return (window.style&WS_CAPTION)==WS_CAPTION&&EqualRect(&native,&window.nativeBounds)&&EqualRect(&frame,&window.frameBounds)
  &&_wcsicmp(imageOf(window.process.value).c_str(),window.path.c_str())==0;
}
std::vector<NativeDecoratedMaximized> nativeDecoratedMaximized(bool& complete){
 struct Search {std::vector<NativeDecoratedMaximized> windows;} search;
 complete=EnumWindows([](HWND hwnd,LPARAM data)->BOOL{
  if(!IsWindowVisible(hwnd)||!IsZoomed(hwnd)||IsIconic(hwnd))return TRUE;
  NativeDecoratedMaximized window;window.hwnd=hwnd;window.style=GetWindowLongPtrW(hwnd,GWL_STYLE);
  if((window.style&WS_CAPTION)!=WS_CAPTION)return TRUE;
  window.exStyle=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);wchar_t name[256]{};
  if(!GetClassNameW(hwnd,name,256))return TRUE;window.className=name;GetWindowThreadProcessId(hwnd,&window.pid);
  if(!window.pid||!GetWindowRect(hwnd,&window.nativeBounds)||FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&window.frameBounds,sizeof(window.frameBounds))))return TRUE;
  window.process=aether::win::Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,window.pid));
  window.birth=birthOf(window.process.value);window.path=imageOf(window.process.value);
  if(!window.path.empty()&&decoratedMaximizedStable(window))reinterpret_cast<Search*>(data)->windows.push_back(std::move(window));
  return TRUE;
 },reinterpret_cast<LPARAM>(&search))!=FALSE;
 return std::move(search.windows);
}
bool decoratedMaximizedNotFullscreen(const std::vector<NativeDecoratedMaximized>& native,const std::vector<aether::win::Window>& collected,bool complete){
 aether::win::WorkAreaSnapshot physical{};const bool physicalKnown=aether::win::captureWorkAreas(physical);
 std::size_t verified=0;bool correct=true;
 for(const auto& window:native){
  if(!decoratedMaximizedStable(window))continue;
  const auto found=std::find_if(collected.begin(),collected.end(),[&](const auto& item){return item.hwnd==window.hwnd&&item.pid==window.pid;});
  if(found==collected.end())continue;
  bool coversPhysicalMonitor=false;
  if(physicalKnown)for(DWORD i=0;i<physical.count;++i){
   const auto& m=physical.entries[i].bounds;const auto& r=window.frameBounds;
   if(r.left<=m.left&&r.top<=m.top&&r.right>=m.right&&r.bottom>=m.bottom)coversPhysicalMonitor=true;
  }
  const auto& n=window.nativeBounds;const auto& f=window.frameBounds;
  std::cout<<"DIAGNOSTIC stable decorated maximized window hwnd="<<reinterpret_cast<std::uintptr_t>(window.hwnd)
   <<" pid="<<window.pid<<" birth="<<window.birth<<" class="<<aether::win::utf8(window.className)
   <<" image="<<aether::win::utf8(window.path)<<" visible=1 zoomed=1 minimized=0 style="<<window.style<<" exStyle="<<window.exStyle
   <<" nativeBounds="<<n.left<<","<<n.top<<","<<n.right<<","<<n.bottom
   <<" frameBounds="<<f.left<<","<<f.top<<","<<f.right<<","<<f.bottom
   <<" physicalMonitorsKnown="<<physicalKnown<<" coversPhysicalMonitor="<<coversPhysicalMonitor
   <<" collectedFullscreen="<<found->fullscreen<<" titlesOmitted=1\n";
  // The existing user window is never moved, resized, shown or activated.
  // Confirm unchanged native bounds/identity after every observation.
  if(!decoratedMaximizedStable(window)){
   std::cout<<"DIAGNOSTIC decorated maximized window changed during observation; conditional coverage unavailable for this window.\n";continue;
  }
  ++verified;
  std::cout<<"DIAGNOSTIC decorated maximized verification hwnd="<<reinterpret_cast<std::uintptr_t>(window.hwnd)<<" pid="<<window.pid<<" heldBirth="<<window.birth<<" nativeBoundsUnchanged=1 frameBoundsUnchanged=1 styleUnchanged=1 ownerAlive=1\n";
  if(found->fullscreen){correct=false;std::cerr<<"FAIL stable decorated maximized window was classified as fullscreen.\n";}
 }
 if(!complete)std::cout<<"DIAGNOSTIC decorated maximized enumeration incomplete; exhaustive conditional coverage unavailable.\n";
 if(!verified)std::cout<<"DIAGNOSTIC no stable collected decorated maximized window observed; conditional native classification coverage unavailable.\n";
 else if(correct)std::cout<<"PASS "<<verified<<" stable decorated maximized window(s) remain nonfullscreen with native bounds unchanged.\n";
 return correct;
}
void describeWindow(const aether::win::Window& window,std::size_t index,const char* role){
 DWORD currentPid=0;const DWORD thread=GetWindowThreadProcessId(window.hwnd,&currentPid);wchar_t name[256]{};
 const bool classKnown=GetClassNameW(window.hwnd,name,256)!=0;
 aether::win::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,window.pid));
 const auto birth=birthOf(process.value);const auto image=imageOf(process.value);
 const bool processAlive=process&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;
 RECT native{},frame{};const bool nativeKnown=GetWindowRect(window.hwnd,&native)!=FALSE;
 const HRESULT frameResult=DwmGetWindowAttribute(window.hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame));
 DWORD cloak=0;const HRESULT cloakResult=DwmGetWindowAttribute(window.hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak));
 const auto& bounds=window.bounds;
 std::cout<<"DIAG eligibleWindow index="<<index<<" role="<<role<<" hwnd="<<reinterpret_cast<std::uintptr_t>(window.hwnd)
  <<" pid="<<window.pid<<" currentPid="<<currentPid<<" thread="<<thread<<" birth="<<birth<<" processAlive="<<processAlive
  <<" windowIdentityMatches="<<(thread&&currentPid==window.pid)<<" classKnown="<<classKnown<<" class="<<aether::win::utf8(name)
  <<" imageSnapshot="<<aether::win::utf8(window.path)<<" imageCurrent="<<aether::win::utf8(image)
  <<" collectedDisplay="<<aether::win::utf8(window.display)<<" collectedFullscreen="<<window.fullscreen
  <<" collectedActive="<<window.active<<" collectedMinimized="<<window.minimized
  <<" visibleNow="<<(IsWindowVisible(window.hwnd)!=FALSE)<<" minimizedNow="<<(IsIconic(window.hwnd)!=FALSE)
  <<" style="<<GetWindowLongPtrW(window.hwnd,GWL_STYLE)<<" exStyle="<<GetWindowLongPtrW(window.hwnd,GWL_EXSTYLE)
  <<" cloakKnown="<<SUCCEEDED(cloakResult)<<" cloak="<<cloak
  <<" collectedBounds="<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom
  <<" nativeBoundsKnown="<<nativeKnown<<" nativeBounds="<<native.left<<","<<native.top<<","<<native.right<<","<<native.bottom
  <<" frameBoundsKnown="<<SUCCEEDED(frameResult)<<" frameBounds="<<frame.left<<","<<frame.top<<","<<frame.right<<","<<frame.bottom<<'\n';
}
int describeDesktop(){
 struct ScopedDpi {
  DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  ~ScopedDpi(){if(previous)SetThreadDpiAwarenessContext(previous);}
 } dpi;
 std::cout<<"RuntimeModeDiagOnlyPrint --describe-desktop tick="<<GetTickCount64()
  <<" dpiContextApplied="<<(dpi.previous!=nullptr)<<" titlesOmitted=1; collector/monitor observations are read-only snapshots.\n";
 aether::win::WindowCollector collector;
 const auto monitors=aether::win::monitors();const auto windows=collector.snapshot();
 const auto fullscreen=aether::win::fullscreenDisplays(windows,monitors);
 std::cout<<"DIAG observed monitors="<<monitors.size()<<" eligibleWindows="<<windows.size()
  <<" policyFullscreenDisplays="<<fullscreen.size()<<'\n';
 for(std::size_t index=0;index<windows.size();++index)describeWindow(windows[index],index,"collector-order");
 for(const auto& monitor:monitors){
  const auto& bounds=monitor.bounds;const auto& work=monitor.work;
  std::cout<<"DIAG monitor device="<<aether::win::utf8(monitor.name)<<" handle="<<reinterpret_cast<std::uintptr_t>(monitor.handle)
   <<" primary="<<monitor.primary<<" dpi="<<monitor.dpi<<" bounds="<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom
   <<" work="<<work.left<<","<<work.top<<","<<work.right<<","<<work.bottom
   <<" policyFullscreen="<<fullscreen.contains(monitor.name)<<'\n';
  bool foundFrontmost=false;
  for(std::size_t index=0;index<windows.size();++index){
   const auto& window=windows[index];if(window.minimized)continue;const auto& r=window.bounds;
   const bool intersects=r.right>bounds.left&&r.left<bounds.right&&r.bottom>bounds.top&&r.top<bounds.bottom;
   if(!intersects)continue;
   const bool covers=r.left<=bounds.left&&r.top<=bounds.top&&r.right>=bounds.right&&r.bottom>=bounds.bottom;
   if(!foundFrontmost){
    foundFrontmost=true;
    std::cout<<"DIAG frontmostEligible device="<<aether::win::utf8(monitor.name)<<" index="<<index
     <<" pid="<<window.pid<<" coversMonitor="<<covers<<'\n';
    describeWindow(window,index,"frontmost-on-monitor");
   }
   if(covers){
    std::cout<<"DIAG coveringWindow device="<<aether::win::utf8(monitor.name)<<" index="<<index
     <<" pid="<<window.pid<<'\n';
    describeWindow(window,index,"covers-monitor");
   }
  }
  if(!foundFrontmost)std::cout<<"DIAG frontmostEligible device="<<aether::win::utf8(monitor.name)<<" none=1\n";
 }
 std::cout<<"RuntimeModeDiagOnlyPrint complete; no UI/input/window/taskbar/workspace mutation requested.\n";return 0;
}
}
int main(int argc,char** argv){
 aether::win::ComApartment com;
 if(argc==2&&std::string_view(argv[1])=="--describe-desktop"){
  try{return describeDesktop();}catch(const std::exception& error){std::cerr<<"RuntimeModeDiagOnlyPrint failed: "<<error.what()<<'\n';return 12;}
 }
 aether::win::setConfigDirectory(aether::win::executablePath().parent_path()/L"test-profiles"/(L"smoke-"+std::to_wstring(GetCurrentProcessId())));
 auto s=aether::win::wide("Tea \xE8\x8C\xB6");
 if(aether::win::utf8(s)!="Tea \xE8\x8C\xB6")return 1;
 for(const auto& m:aether::win::monitors())if(m.bounds.width()<=0||m.bounds.height()<=0||m.dpi==0)return 2;
 auto config=aether::win::configDirectory();if(config.empty())return 3;
 if(aether::win::windowAlive(nullptr,0))return 4;
 const auto expectedExplorer=explorerImage();bool occlusionEnumerationComplete=false;
 auto occlusionFrames=nativeOcclusionFrames(expectedExplorer,occlusionEnumerationComplete);
 bool decoratedEnumerationComplete=false;auto decoratedMaximized=nativeDecoratedMaximized(decoratedEnumerationComplete);
 auto windows=aether::win::windows();
 for(const auto& w:windows)if(w.title.empty()||!w.hwnd||!w.pid)return 5;
 aether::win::RecoverySnapshot snapshot{};
 if(snapshot.armed!=0||snapshot.count!=0)return 6;
 // Ordinary smoke never requests native activation; armed acceptance is opt-in.

 auto before=nativeBars();
 APPBARDATA state{sizeof(state)};state.hWnd=FindWindowW(L"Shell_TrayWnd",nullptr);
 auto originalState=state.hWnd?SHAppBarMessage(ABM_GETSTATE,&state):0;
 aether::win::TaskbarGuard guard;
 if(snapshot.nativeAllowed!=0||aether::win::TaskbarGuardOptions{}.enableNativeForValidation||guard.enabled()||guard.healthy())return 7;
 guard.refresh();if(!guard.setEnabled(false))return 8;
 for(const auto& bar:before){
  if(!aether::win::windowAlive(bar.hwnd,bar.pid)||IsWindowVisible(bar.hwnd)!=(bar.visible?TRUE:FALSE))return 9;
 }
 if(state.hWnd&&SHAppBarMessage(ABM_GETSTATE,&state)!=originalState)return 10;
 if(!occlusionExcluded(occlusionFrames,expectedExplorer,windows,occlusionEnumerationComplete))return 11;
 if(!decoratedMaximizedNotFullscreen(decoratedMaximized,windows,decoratedEnumerationComplete))return 13;
 std::cout<<"Windows read-only smoke checks passed; no activation requested and native state unchanged.\n";
 return 0;
}
