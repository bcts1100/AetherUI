#include "platform/desktop.hpp"
#include "platform/display_policy.hpp"
#include "platform/config.hpp"
#include <wtsapi32.h>
#include <iostream>
#include <functional>
#include <cstdint>
int failures=0,checks=0;
void check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;}
bool waitFor(const std::function<bool()>& predicate,int milliseconds=5000){auto deadline=GetTickCount64()+milliseconds;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(predicate())return true;Sleep(30);}while(GetTickCount64()<deadline);return false;}
std::vector<HWND> owned(DWORD pid,const wchar_t* title){struct C{DWORD pid;const wchar_t* title;std::vector<HWND> out;}c{pid,title,{}};EnumWindows([](HWND h,LPARAM data)->BOOL{auto& c=*reinterpret_cast<C*>(data);DWORD p=0;GetWindowThreadProcessId(h,&p);if(p==c.pid&&aether::win::windowText(h)==c.title)c.out.push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&c));return c.out;}
int visible(DWORD pid,const wchar_t* title){auto list=owned(pid,title);return static_cast<int>(std::count_if(list.begin(),list.end(),[](HWND h){return IsWindowVisible(h)!=FALSE;}));}
ULONGLONG lifecycleBirth(HANDLE process){
 FILETIME created{},exited{},kernel{},user{};
 if(!process||!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
 return (static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
std::wstring lifecycleImage(HANDLE process){
 wchar_t value[32768]{};DWORD count=32768;
 return process&&QueryFullProcessImageNameW(process,0,value,&count)?std::wstring(value,count):std::wstring{};
}
void describeLifecycleWindow(const aether::win::Window& window,std::size_t index,const char* role){
 wchar_t name[256]{};GetClassNameW(window.hwnd,name,256);DWORD currentPid=0;GetWindowThreadProcessId(window.hwnd,&currentPid);
 aether::win::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,window.pid));
 DWORD cloak=0;const HRESULT cloakResult=DwmGetWindowAttribute(window.hwnd,DWMWA_CLOAKED,&cloak,sizeof(cloak));
 const auto& r=window.bounds;
 std::cout<<"DIAG lifecycle eligibleWindow index="<<index<<" role="<<role<<" hwnd="<<reinterpret_cast<std::uintptr_t>(window.hwnd)
  <<" pid="<<window.pid<<" currentPid="<<currentPid<<" birth="<<lifecycleBirth(process.value)
  <<" processAlive="<<(process&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT)
  <<" class="<<aether::win::utf8(name)<<" imageSnapshot="<<aether::win::utf8(window.path)
  <<" imageCurrent="<<aether::win::utf8(lifecycleImage(process.value))
  <<" display="<<aether::win::utf8(window.display)<<" bounds="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom
  <<" collectedFullscreen="<<window.fullscreen<<" collectedActive="<<window.active<<" collectedMinimized="<<window.minimized
  <<" visibleNow="<<(IsWindowVisible(window.hwnd)!=FALSE)<<" minimizedNow="<<(IsIconic(window.hwnd)!=FALSE)
  <<" style="<<GetWindowLongPtrW(window.hwnd,GWL_STYLE)<<" exStyle="<<GetWindowLongPtrW(window.hwnd,GWL_EXSTYLE)
  <<" cloakKnown="<<SUCCEEDED(cloakResult)<<" cloak="<<cloak<<'\n';
}
void describeInitialPanelFailure(DWORD appPid,HANDLE appProcess,HWND controller){
 // Called only after the original first assertion has already failed.
 // No extra observation or delay enters a passing fixture or its prerequisites.
 try{
  using namespace aether::win;
  std::cout<<"RuntimeModeDiagOnlyPrint lifecycle stage=afterFirstCreationTimeout tick="<<GetTickCount64()
   <<" collectorPid="<<GetCurrentProcessId()<<" appPid="<<appPid<<" heldProcessPid="<<GetProcessId(appProcess)
   <<" appBirth="<<lifecycleBirth(appProcess)<<" appAlive="<<(WaitForSingleObject(appProcess,0)==WAIT_TIMEOUT)
   <<" appImage="<<utf8(lifecycleImage(appProcess))<<" controller="<<reinterpret_cast<std::uintptr_t>(controller)
   <<" lifecyclePhase="<<reinterpret_cast<std::uintptr_t>(GetPropW(controller,L"Aether.LifecyclePhase"))
   <<" privateQuiescenceUnavailable=1 titlesOmitted=1\n";
  WindowCollector collector;const auto current=monitors();const auto windows=collector.snapshot();
  const auto fullscreen=fullscreenDisplays(windows,current);
  std::cout<<"DIAG lifecycle eligibleWindows="<<windows.size()<<" monitors="<<current.size()<<" policyFullscreenDisplays="<<fullscreen.size()<<'\n';
  for(std::size_t index=0;index<windows.size();++index)describeLifecycleWindow(windows[index],index,"collector-order");
  for(const auto& monitor:current){
   const auto& b=monitor.bounds;const auto& w=monitor.work;
   std::cout<<"DIAG lifecycle monitor device="<<utf8(monitor.name)<<" primary="<<monitor.primary<<" dpi="<<monitor.dpi
    <<" bounds="<<b.left<<","<<b.top<<","<<b.right<<","<<b.bottom<<" work="<<w.left<<","<<w.top<<","<<w.right<<","<<w.bottom
    <<" policyFullscreen="<<fullscreen.contains(monitor.name)<<'\n';
   bool first=false;
   for(std::size_t index=0;index<windows.size();++index){
    const auto& window=windows[index];if(window.minimized)continue;const auto& r=window.bounds;
    if(r.right<=b.left||r.left>=b.right||r.bottom<=b.top||r.top>=b.bottom)continue;
    const bool covers=r.left<=b.left&&r.top<=b.top&&r.right>=b.right&&r.bottom>=b.bottom;
    if(!first){
     first=true;std::cout<<"DIAG lifecycle frontmostEligible device="<<utf8(monitor.name)<<" index="<<index<<" pid="<<window.pid<<" coversMonitor="<<covers<<'\n';
     describeLifecycleWindow(window,index,"frontmost-on-monitor");
    }
    if(covers){
     std::cout<<"DIAG lifecycle coveringWindow device="<<utf8(monitor.name)<<" index="<<index<<" pid="<<window.pid<<'\n';
     describeLifecycleWindow(window,index,"covers-monitor");
    }
   }
   if(!first)std::cout<<"DIAG lifecycle frontmostEligible device="<<utf8(monitor.name)<<" none=1\n";
  }
  for(const auto* title:{L"Aether Top Bar",L"Aether Dock"})for(auto panel:owned(appPid,title)){
   RECT r{};const bool boundsKnown=GetWindowRect(panel,&r)!=FALSE;wchar_t name[256]{};GetClassNameW(panel,name,256);
   DWORD pid=0;GetWindowThreadProcessId(panel,&pid);DWORD cloak=0;const HRESULT cloakResult=DwmGetWindowAttribute(panel,DWMWA_CLOAKED,&cloak,sizeof(cloak));
   MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);const auto display=MonitorFromWindow(panel,MONITOR_DEFAULTTONULL);
   const bool monitorKnown=display&&GetMonitorInfoW(display,reinterpret_cast<MONITORINFO*>(&monitor));
   std::cout<<"DIAG lifecycle ownedPanel kind="<<(wcscmp(title,L"Aether Dock")==0?"dock":"top")
    <<" hwnd="<<reinterpret_cast<std::uintptr_t>(panel)<<" pid="<<pid<<" owned="<<(pid==appPid)
    <<" class="<<utf8(name)<<" monitorKnown="<<monitorKnown<<" display="<<utf8(monitor.szDevice)
    <<" primary="<<(monitorKnown&&(monitor.dwFlags&MONITORINFOF_PRIMARY)!=0)<<" dpi="<<GetDpiForWindow(panel)
    <<" visible="<<(IsWindowVisible(panel)!=FALSE)<<" minimized="<<(IsIconic(panel)!=FALSE)
    <<" boundsKnown="<<boundsKnown<<" bounds="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom
    <<" style="<<GetWindowLongPtrW(panel,GWL_STYLE)<<" exStyle="<<GetWindowLongPtrW(panel,GWL_EXSTYLE)
    <<" cloakKnown="<<SUCCEEDED(cloakResult)<<" cloak="<<cloak<<'\n';
  }
  std::cout<<"RuntimeModeDiagOnlyPrint lifecycle complete; no UI/input/window/taskbar/workspace mutation requested.\n";
 }catch(const std::exception& error){std::cerr<<"RuntimeModeDiagOnlyPrint lifecycle diagnostic unavailable: "<<error.what()<<'\n';}
}
LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_CLOSE){DestroyWindow(h);return 0;}return DefWindowProcW(h,m,w,l);}
int main(int argc,char** argv){
 using namespace aether;using namespace aether::win;ComApartment com;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if((argc!=2&&argc!=3)||FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"A binary argument and no existing Aether controller are required.\n";return 2;}
 auto directory=executablePath().parent_path()/L"test-profiles"/(L"lifecycle-"+std::to_wstring(GetCurrentProcessId()));setConfigDirectory(directory);Config configuration;configuration.hideTaskbar=false;configuration.defaults.dockGap=32;if(argc==3)configuration.defaults.edge=std::string(argv[2])=="left"?Edge::Left:Edge::Right;saveConfig(configuration);
 auto before=monitors();if(before.empty())return 3;auto expected=static_cast<int>(before.size());
 std::wstring command=L"\""+wide(argv[1])+L"\" --settings-dir \""+directory.wstring()+L"\"";
 STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 4;Handle process(pi.hProcess),thread(pi.hThread);
 HWND controller=nullptr;const bool initialPanels=waitFor([&]{auto v=owned(pi.dwProcessId,L"Aether UI Controller");if(v.empty())return false;controller=v.front();return visible(pi.dwProcessId,L"Aether Top Bar")==expected&&visible(pi.dwProcessId,L"Aether Dock")==expected;},10000);
 check(initialPanels,"controller creates panels for every connected monitor");
 if(!initialPanels)describeInitialPanelFailure(pi.dwProcessId,process.value,controller);
 check(waitFor([&]{auto docks=owned(pi.dwProcessId,L"Aether Dock");if(static_cast<int>(docks.size())!=expected)return false;for(auto h:docks){RECT r{};GetWindowRect(h,&r);if((configuration.defaults.edge==Edge::Bottom?r.bottom-r.top:r.right-r.left)!=MulDiv(configuration.defaults.iconSize+24,static_cast<int>(GetDpiForWindow(h)),96))return false;}return true;}),"reserved dock preserves icon thickness at maximum gap");
 if(!controller){std::cerr<<"Owned process did not create controller; left for diagnosis.\n";return 5;}
 PostMessageW(controller,WM_HOTKEY,2,0);HWND editor=nullptr;check(waitFor([&]{auto v=owned(pi.dwProcessId,L"Aether UI - Desktop Studio");if(v.empty())return false;editor=v.front();return IsWindowVisible(editor)!=FALSE;}),"editor opens through controller");
 PostMessageW(controller,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==0&&visible(pi.dwProcessId,L"Aether Top Bar")==0;}),"synthetic session lock suppresses all panels");Sleep(100);check(!IsWindowVisible(editor),"synthetic session lock closes Desktop Studio");
 PostMessageW(controller,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==expected&&visible(pi.dwProcessId,L"Aether Top Bar")==expected;}),"synthetic session unlock restores panels");if(editor)PostMessageW(editor,WM_CLOSE,0,0);
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProc;wc.lpszClassName=L"Aether.Spanning.Fixture";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));RegisterClassW(&wc);Rect all=before.front().bounds;for(const auto& m:before){all.left=std::min(all.left,m.bounds.left);all.top=std::min(all.top,m.bounds.top);all.right=std::max(all.right,m.bounds.right);all.bottom=std::max(all.bottom,m.bounds.bottom);}
 HWND span=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"Aether fullscreen validation fixture",WS_POPUP,all.left,all.top,all.width(),all.height(),nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(span,SW_SHOW);SetForegroundWindow(span);PostMessageW(controller,RefreshMessage,0,0);
 check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==0&&visible(pi.dwProcessId,L"Aether Top Bar")==0;}),"real spanning fixture suppresses panels on every display");ShowWindow(span,SW_HIDE);DestroyWindow(span);PostMessageW(controller,RefreshMessage,0,0);check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==expected&&visible(pi.dwProcessId,L"Aether Top Bar")==expected;}),"closing spanning fixture restores every panel");
 auto tops=owned(pi.dwProcessId,L"Aether Top Bar");if(!tops.empty()){
  auto top=tops.front();RECT bounds{};GetWindowRect(top,&bounds);PostMessageW(top,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(top),MAKELPARAM(bounds.left+30,bounds.top+15));
  bool menu=waitFor([&]{HWND h=FindWindowW(L"#32768",nullptr);DWORD p=0;if(h)GetWindowThreadProcessId(h,&p);return p==pi.dwProcessId;});check(menu,"native panel context menu opens");
  if(menu){PostMessageW(controller,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==0&&visible(pi.dwProcessId,L"Aether Top Bar")==0;}),"session lock cancels menu and suppresses panels");PostMessageW(controller,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0);check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Dock")==expected&&visible(pi.dwProcessId,L"Aether Top Bar")==expected;}),"session unlock after menu restores panels");PostMessageW(top,WM_CANCELMODE,0,0);waitFor([&]{return FindWindowW(L"#32768",nullptr)==nullptr;},1000);PostMessageW(top,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(top),MAKELPARAM(bounds.left+30,bounds.top+15));menu=waitFor([&]{HWND h=FindWindowW(L"#32768",nullptr);DWORD p=0;if(h)GetWindowThreadProcessId(h,&p);return p==pi.dwProcessId;});}
  if(menu){PostMessageW(controller,RegisterWindowMessageW(L"TaskbarCreated"),0,0);PostMessageW(controller,WM_DISPLAYCHANGE,32,0);PostMessageW(controller,WM_SETTINGCHANGE,0,0);Sleep(200);check(IsWindow(top)!=FALSE,"menu owner survives queued shell/display/settings rebuild");PostMessageW(top,WM_CANCELMODE,0,0);}
  check(waitFor([&]{return visible(pi.dwProcessId,L"Aether Top Bar")==expected&&visible(pi.dwProcessId,L"Aether Dock")==expected;}),"panels remain valid after menu rebuild");
  tops=owned(pi.dwProcessId,L"Aether Top Bar");if(!tops.empty()){top=tops.front();GetWindowRect(top,&bounds);PostMessageW(top,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(top),MAKELPARAM(bounds.left+30,bounds.top+15));Sleep(200);}
 }
 PostMessageW(controller,WM_CLOSE,0,0);bool exited=waitFor([&]{return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;},8000);if(!exited){for(auto h:owned(pi.dwProcessId,L"Aether Top Bar"))PostMessageW(h,WM_CANCELMODE,0,0);PostMessageW(controller,WM_CLOSE,0,0);exited=waitFor([&]{return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;},4000);}
 DWORD code=1;if(exited)GetExitCodeProcess(process.value,&code);check(exited&&code==0,"quit during native menu exits cooperatively with code zero");std::vector<Monitor> after;bool restored=waitFor([&]{after=monitors();return before==after;},5000);check(restored,"all original monitor work areas restore after lifecycle tests");if(!restored){for(const auto& m:before)std::cout<<"before "<<utf8(m.name)<<" dpi "<<m.dpi<<" work "<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<"\n";for(const auto& m:after)std::cout<<"after "<<utf8(m.name)<<" dpi "<<m.dpi<<" work "<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<"\n";}
 std::cout<<checks-failures<<'/'<<checks<<" desktop lifecycle checks passed; session/display messages are synthetic, spanning fixture is real.\n";return failures?1:0;
}
