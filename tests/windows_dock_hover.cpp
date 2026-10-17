#include "app.hpp"
#include "platform/config.hpp"
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <thread>

using namespace aether;
namespace {
constexpr UINT AddFixture=WM_APP+100,AddPin=WM_APP+101,Inspect=WM_APP+102,HoldInteraction=WM_APP+103,ResumeInteraction=WM_APP+104,OpenMarketplace=WM_APP+105,CloseMarketplace=WM_APP+106;
struct Inspection {DWORD fixturePid{};HMONITOR monitor{};std::size_t fixtureTasks{},groupCount{};std::string signature;};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
bool waitFor(const std::function<bool()>& predicate,int milliseconds=5000){auto deadline=GetTickCount64()+milliseconds;do{pump();if(predicate())return true;Sleep(10);}while(GetTickCount64()<deadline);return false;}
void settle(int milliseconds){auto deadline=GetTickCount64()+milliseconds;while(GetTickCount64()<deadline){pump();Sleep(10);}}
HWND owned(DWORD expected,const wchar_t* title){
 struct Search {DWORD pid;const wchar_t* title;HWND result{};} search{expected,title};
 EnumWindows([](HWND h,LPARAM data)->BOOL{auto& find=*reinterpret_cast<Search*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==find.pid&&win::windowText(h)==find.title){find.result=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;
}
std::vector<HWND> visibleOwnedDocks(DWORD expected){
 struct Search {DWORD pid;std::vector<HWND> windows;} search{expected,{}};
 require(EnumWindows([](HWND h,LPARAM data)->BOOL{
  auto& find=*reinterpret_cast<Search*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
  if(pid==find.pid&&IsWindowVisible(h)&&win::windowText(h)==L"Aether Dock")find.windows.push_back(h);
  return TRUE;
 },reinterpret_cast<LPARAM>(&search))!=FALSE,"Owned dock enumeration failed");
 return search.windows;
}
HWND accessibleOwnedDock(DWORD expected){
 HWND selected{};bool selectedPrimary=false;
 for(auto dock:visibleOwnedDocks(expected)){
  RECT rect{};if(!GetWindowRect(dock,&rect))continue;
  const auto dpi=win::dpiFor(dock);POINT target{rect.left+MulDiv(82,dpi,96),(rect.top+rect.bottom)/2};
  const auto monitor=MonitorFromWindow(dock,MONITOR_DEFAULTTONULL);MONITORINFOEXW info{};info.cbSize=sizeof(info);
  if(!monitor||!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info)))continue;
  const auto under=WindowFromPoint(target);
  const bool unoccluded=target.x>=rect.left&&target.x<rect.right&&target.y>=rect.top&&target.y<rect.bottom&&under==dock;
  const bool primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
  std::cerr<<"DIAGNOSTIC dock candidate hwnd="<<reinterpret_cast<std::uintptr_t>(dock)<<" monitor="<<win::utf8(info.szDevice)
   <<" primary="<<primary<<" dpi="<<dpi<<" rect="<<rect.left<<","<<rect.top<<","<<rect.right<<","<<rect.bottom
   <<" pinTarget="<<target.x<<","<<target.y<<" unoccluded="<<unoccluded<<'\n';
  if(!unoccluded){
   wchar_t name[256]{};GetClassNameW(under,name,256);DWORD pid=0;GetWindowThreadProcessId(under,&pid);
   std::cerr<<"DIAGNOSTIC blocked pin target class="<<win::utf8(name)<<" title="<<win::utf8(win::windowText(under))<<" pid="<<pid<<'\n';
  }else if(!selected||(primary&&!selectedPrimary)){selected=dock;selectedPrimary=primary;}
 }
 if(selected)std::cerr<<"DIAGNOSTIC selected unoccluded owned dock hwnd="<<reinterpret_cast<std::uintptr_t>(selected)<<" primary="<<selectedPrimary<<'\n';
 return selected;
}
RECT bounds(HWND window){RECT value{};require(GetWindowRect(window,&value)!=FALSE,"Native owned bounds unavailable");return value;}
bool actualDockPointer(HWND dock,POINT expected,const char* stage){
 POINT cursor{};GetCursorPos(&cursor);auto under=WindowFromPoint(cursor);
 if(cursor.x==expected.x&&cursor.y==expected.y&&under==dock)return true;
 auto current=bounds(dock);wchar_t name[256]{};GetClassNameW(under,name,256);DWORD pid=0;GetWindowThreadProcessId(under,&pid);RECT underBounds{};GetWindowRect(under,&underBounds);
 std::cerr<<"DIAGNOSTIC "<<stage<<" dock="<<current.left<<","<<current.top<<","<<current.right<<","<<current.bottom<<" expected="<<expected.x<<","<<expected.y<<" cursor="<<cursor.x<<","<<cursor.y<<" onDock="<<(under==dock)<<'\n';
 std::cerr<<"DIAGNOSTIC underPointer class="<<win::utf8(name)<<" title="<<win::utf8(win::windowText(under))<<" pid="<<pid<<" rect="<<underBounds.left<<","<<underBounds.top<<","<<underBounds.right<<","<<underBounds.bottom<<'\n';return false;
}
std::vector<HWND> fixtureWindows;
LRESULT CALLBACK fixtureProcedure(HWND window,UINT message,WPARAM w,LPARAM l);
void addFixture(){
 auto name=L"AetherHoverFixture "+std::to_wstring(fixtureWindows.size());
 auto window=CreateWindowExW(WS_EX_APPWINDOW,L"Aether.Hover.Fixture",name.c_str(),WS_OVERLAPPEDWINDOW,50,80,560,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 require(window!=nullptr,"Foreign fixture window could not be created");fixtureWindows.push_back(window);ShowWindow(window,SW_SHOWMINNOACTIVE);
}
LRESULT CALLBACK fixtureProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==AddFixture){addFixture();return 0;}
 if(message==WM_CLOSE){for(auto source:fixtureWindows)if(IsWindow(source))DestroyWindow(source);PostQuitMessage(0);return 0;}
 return DefWindowProcW(window,message,w,l);
}
int fixtureMain(){
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProcedure;wc.lpszClassName=L"Aether.Hover.Fixture";require(RegisterClassW(&wc)!=0,"Fixture class registration failed");
 for(int i=0;i<3;++i)addFixture();MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
LRESULT CALLBACK actionProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==OpenMarketplace){auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));app->openMarketplace();return 0;}
 if(message==CloseMarketplace){auto market=FindWindowW(L"Aether.Surface.v3",L"Aether Marketplace");DWORD owner=0;GetWindowThreadProcessId(market,&owner);if(owner==GetCurrentProcessId())SendMessageW(market,WM_CLOSE,0,0);return 0;}
 if(message==HoldInteraction||message==ResumeInteraction){auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==HoldInteraction)app->beginInteraction();else app->endInteraction();return 0;}
 if(message==WM_NCCREATE){SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));}
 if(message==Inspect){
  // SendMessage synchronizes this copy on App's UI thread; the observer never
  // reads App's changing group/window vectors directly.
  auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));auto& report=*reinterpret_cast<Inspection*>(l);
  report.fixtureTasks=report.groupCount=0;report.signature.clear();
  for(const auto& display:app->displays())if(display.handle==report.monitor){
   auto groups=app->dockGroups(display.name);report.groupCount=groups.size();
   for(std::size_t i=0;i<groups.size();++i){const auto& group=groups[i];report.signature+=std::to_string(i)+":"+group.app.identity+":"+std::to_string(group.tasks.size())+";";
    if(i==0)for(auto index:group.tasks)if(index<app->windows().size()&&app->windows()[index].pid==report.fixturePid)++report.fixtureTasks;
   }
  }
  return 0;
 }
 if(message==AddPin){
  auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));auto config=app->config();auto target=win::executablePath().parent_path()/L"AetherProvider.exe";
  config.pins.push_back({"Layout-only pinned app",win::utf8(target.wstring()),win::utf8(win::lower(target.wstring()))});app->apply(std::move(config));return 0;
 }
 return DefWindowProcW(window,message,w,l);
}
struct ForeignFixture {
 win::Handle process,thread;DWORD pid{};HWND first{};
 ForeignFixture(){
  auto executable=win::executablePath();std::wstring command=L"\""+executable.wstring()+L"\" --fixture";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION info{};
  require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=FALSE,"Owned foreign fixture process could not start");process.reset(info.hProcess);thread.reset(info.hThread);pid=info.dwProcessId;
  require(waitFor([&]{first=owned(pid,L"AetherHoverFixture 0");return first&&owned(pid,L"AetherHoverFixture 2");}),"Foreign fixture did not become ready");
 }
 void finish(){
  if(first&&win::windowAlive(first,pid))PostMessageW(first,WM_CLOSE,0,0);
  require(!process||waitFor([&]{return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;},10000),"Owned fixture did not exit after its normal close request; left for diagnosis");
  DWORD code=0;require(!process||(GetExitCodeProcess(process.value,&code)&&code==0),"Owned fixture returned a nonzero exit code");first=nullptr;
 }
 ~ForeignFixture(){try{finish();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';}}
};
std::vector<std::pair<HWND,bool>> taskbars(){std::vector<std::pair<HWND,bool>> result;EnumWindows([](HWND h,LPARAM data)->BOOL{wchar_t name[256]{};GetClassNameW(h,name,256);if(wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0)reinterpret_cast<std::vector<std::pair<HWND,bool>>*>(data)->emplace_back(h,IsWindowVisible(h)!=FALSE);return TRUE;},reinterpret_cast<LPARAM>(&result));return result;}
}
int main(int argc,char** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc==2&&std::string_view(argv[1])=="--fixture")return fixtureMain();
 win::ComApartment apartment;POINT initialCursor{};GetCursorPos(&initialCursor);auto initialMonitors=win::monitors();auto initialBars=taskbars();
 std::filesystem::path directory;int failures=0;
 try{
  require(!FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"),"Existing Aether instance would interfere with the native hover regression");
  ForeignFixture fixture;
  directory=win::executablePath().parent_path()/L"test-profiles"/(L"dock-hover-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));win::setConfigDirectory(directory);
  Config config;config.hideTaskbar=false;config.windowPreviews=true;config.defaults.topBar=false;config.defaults.reserveDock=false;config.defaults.monitorOnly=false;config.defaults.iconSize=48;
  auto image=win::executablePath().wstring();config.pins={{"Owned hover fixture",win::utf8(image),win::utf8(win::lower(image))}};win::saveConfig(config);
  App app;WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=actionProcedure;wc.lpszClassName=L"Aether.Hover.Test.Actions";require(RegisterClassW(&wc)!=0,"Owned action class could not register");
  auto actions=CreateWindowExW(0,wc.lpszClassName,L"Owned hover test actions",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,&app);require(actions!=nullptr,"Owned action dispatcher could not be created");
  std::exception_ptr error;std::thread observer([&]{try{
   HWND controller{},dock{},flyout{};require(waitFor([&]{controller=owned(GetCurrentProcessId(),L"Aether UI Controller");return controller&&!visibleOwnedDocks(GetCurrentProcessId()).empty();},10000),"Native app panels did not start");
   auto area=initialMonitors.front().work;require(SetCursorPos(area.left+16,area.top+16)!=FALSE,"Could not move the real cursor away");settle(120);
   dock=accessibleOwnedDock(GetCurrentProcessId());
   require(dock!=nullptr,"Real dock hover unavailable: every visible owned dock pin target is occluded; inspect native occlusion diagnostics and rerun on an unlocked desktop");
   RECT readyBounds{};std::string readySignature;ULONGLONG stableSince=0;Inspection ready{fixture.pid,MonitorFromWindow(dock,MONITOR_DEFAULTTONULL)};
   require(waitFor([&]{
    SendMessageW(actions,Inspect,0,reinterpret_cast<LPARAM>(&ready));auto current=bounds(dock);auto now=GetTickCount64();
    if(ready.fixtureTasks!=3){stableSince=0;return false;}
    if(!stableSince||!EqualRect(&readyBounds,&current)||readySignature!=ready.signature){readyBounds=current;readySignature=ready.signature;stableSince=now;return false;}
    return now-stableSince>=350;
   },10000),"Initial dock geometry and three-window fixture group did not become stable");
   auto original=bounds(dock);auto dpi=win::dpiFor(dock);POINT target{original.left+MulDiv(82,dpi,96),(original.top+original.bottom)/2};
   require(SetCursorPos(target.x,target.y)!=FALSE,"Could not hover the native live pin");
   require(actualDockPointer(dock,target,"initial hover prerequisite"),"Real dock hover unavailable: pointer is not over the owned dock; inspect native occlusion diagnostics and rerun on an unlocked desktop");
   // Deliver the delayed notification from an earlier tracking generation
   // after this real entry. The actual stationary pointer remains on the pin.
   settle(80);POINT client=target;ScreenToClient(dock,&client);
   SendMessageW(dock,WM_MOUSEMOVE,0,MAKELPARAM(client.x,client.y));
   SendMessageW(dock,WM_MOUSELEAVE,0,0);
   auto started=GetTickCount64();
   bool opened=waitFor([&]{flyout=owned(GetCurrentProcessId(),L"Aether Dock Windows");return flyout&&IsWindowVisible(flyout);},2500);
   if(!opened){
    auto current=bounds(dock);SendMessageW(actions,Inspect,0,reinterpret_cast<LPARAM>(&ready));
    std::cerr<<"DIAGNOSTIC hover +"<<(GetTickCount64()-started)<<"ms dock="<<current.left<<","<<current.top<<","<<current.right<<","<<current.bottom<<" groups="<<ready.groupCount<<" fixtureTasks="<<ready.fixtureTasks<<" dpi="<<dpi<<'\n';
    require(actualDockPointer(dock,target,"initial hover timeout"),"Real dock hover unavailable: native pointer ownership changed during the opening wait");
   }
   require(opened,"Initial actual live-group hover did not open");
   std::cout<<"PASS delayed mouse-leave cannot cancel a stationary real live-group hover\n";
   require(win::windowText(GetDlgItem(flyout,100)).starts_with(L"AetherHoverFixture"),"Initial flyout did not contain the foreign owned source group");
   SendMessageW(flyout,WM_CLOSE,0,0);require(!IsWindowVisible(flyout),"Intentional close did not hide the flyout");
   require(SetCursorPos(area.left+16,area.top+16)!=FALSE,"Could not leave the dismissed pin");settle(120);
   SendMessageW(actions,HoldInteraction,0,0);
   original=bounds(dock);target={original.left+MulDiv(82,dpi,96),(original.top+original.bottom)/2};
   const bool movedDuringHold=SetCursorPos(target.x,target.y)!=FALSE;settle(450);
   SendMessageW(actions,ResumeInteraction,0,0);
   require(movedDuringHold&&actualDockPointer(dock,target,"interaction resume"),"Interaction resume lost the actual stationary pointer");
   require(waitFor([&]{return IsWindowVisible(flyout)!=FALSE;},2500),"Ending a transient interaction never rearmed stationary live hover");
   std::cout<<"PASS stationary live hover resumes after transient interaction without pointer movement\n";
   SendMessageW(flyout,WM_CLOSE,0,0);require(SetCursorPos(area.left+16,area.top+16)!=FALSE,"Could not leave resumed pin");settle(120);
   SendMessageW(actions,OpenMarketplace,0,0);require(SetCursorPos(target.x,target.y)!=FALSE,"Could not hover while Marketplace was open");settle(450);SendMessageW(actions,CloseMarketplace,0,0);
   require(actualDockPointer(dock,target,"closed Marketplace"),"Marketplace closure lost its stationary pointer");require(waitFor([&]{return IsWindowVisible(flyout)!=FALSE;},2500),"Closing Marketplace never rearmed stationary dock hover");std::cout<<"PASS stationary hover resumes after Marketplace closes\n";
   SendMessageW(flyout,WM_CLOSE,0,0);require(SetCursorPos(area.left+16,area.top+16)!=FALSE,"Could not leave after Marketplace check");settle(120);
   original=bounds(dock);target={original.left+MulDiv(82,dpi,96),(original.top+original.bottom)/2};require(SetCursorPos(target.x,target.y)!=FALSE,"Could not reenter the live pin");settle(100);
   SendMessageW(actions,AddPin,0,0);
   require(waitFor([&]{auto current=bounds(dock);return current.right-current.left!=original.right-original.left;},2000),"Regression did not change native dock geometry");
   POINT actual{};GetCursorPos(&actual);auto changed=bounds(dock);
   require(actualDockPointer(dock,target,"stationary relayout"),"Relayout test lost its stationary actual dock pointer");
   int left=changed.left+MulDiv(68,dpi,96),right=changed.left+MulDiv(124,dpi,96);require(actual.x>=left&&actual.x<right,"Stationary pointer no longer covers the fixture pin after relayout");
   require(waitFor([&]{return IsWindowVisible(flyout)!=FALSE;},2500),"Relayout canceled hover and never rearmed for the same live group");
   require(win::windowText(GetDlgItem(flyout,100)).starts_with(L"AetherHoverFixture"),"Relayout opened a different application group");std::cout<<"PASS stationary live hover reconciles after dock relayout\n";
   SendMessageW(flyout,WM_CLOSE,0,0);require(!IsWindowVisible(flyout),"Dismissal fixture did not close");
   PostMessageW(fixture.first,AddFixture,0,0);require(waitFor([&]{return owned(fixture.pid,L"AetherHoverFixture 3")!=nullptr;}),"Dismissal test did not change the foreign group snapshot");PostMessageW(controller,win::RefreshMessage,0,0);settle(1200);
   GetCursorPos(&actual);require(actual.x==target.x&&actual.y==target.y,"Dismissal check did not retain the stationary real cursor");require(!IsWindowVisible(flyout),"Dirty group snapshot reopened a deliberately dismissed flyout without pointer movement");std::cout<<"PASS intentional dismissal remains closed through a dirty group snapshot\n";
  }catch(...){error=std::current_exception();}app.quit();});
  int result=app.run();observer.join();DestroyWindow(actions);require(result==0,"Native app returned a nonzero exit code");fixture.finish();if(error)std::rethrow_exception(error);
 }catch(const std::exception& error){++failures;std::cerr<<"FAIL native dock hover lifecycle: "<<error.what()<<'\n';}
 SetCursorPos(initialCursor.x,initialCursor.y);
 auto restored=waitFor([&]{return win::monitors()==initialMonitors;},5000);if(!restored){++failures;std::cerr<<"FAIL native work areas were not restored\n";}
 for(auto [bar,visible]:initialBars)if(visible&&!IsWindowVisible(bar)){++failures;std::cerr<<"FAIL native taskbar visibility was not retained\n";}
 win::setConfigDirectory({});if(!directory.empty()){std::error_code error;std::filesystem::remove_all(directory,error);}return failures?1:0;
}
