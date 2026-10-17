#include "core/overview.hpp"
#include "ui/activities.hpp"
#include "platform/config.hpp"
#include "platform/desktop.hpp"
#include <algorithm>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <oleacc.h>

using namespace aether;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
bool waitFor(const std::function<bool()>& predicate,int timeout=5000){auto deadline=GetTickCount64()+timeout;do{pump();if(predicate())return true;MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}while(GetTickCount64()<deadline);return false;}
HWND owned(DWORD pid,const wchar_t* title){struct Find{DWORD pid;const wchar_t* title;HWND found{};}find{pid,title};EnumWindows([](HWND h,LPARAM p)->BOOL{auto& f=*reinterpret_cast<Find*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==f.pid&&win::windowText(h)==f.title){f.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&find));return find.found;}
void gridBoundsAndPaging(){
 auto grid=overviewLayout(720,630);require(grid.columns==3&&grid.rows==2&&grid.capacity()==6,"Standard overview does not expose a two-dimensional page");
 require(grid.hit(130,190)==0&&grid.hit(365,190)==1&&grid.hit(600,430)==5,"Grid tile hit testing does not match two-dimensional coordinates");
 require(grid.hit(248,190)==-1&&grid.hit(130,600)==-1,"Grid gutter or footer accepts a tile hit");
 require(overviewPageOffset(7,20,grid)==6&&overviewPageOffset(19,20,grid)==18,"Selected window is not on its bounded page");
 require(overviewLayout(3000,3000).capacity()==12,"Large overview exceeds the registration bound");
 require(overviewLayout(100,180).capacity()==0,"Tiny overview exposes incomplete tiles");
 auto shortGrid=overviewLayout(340,310);require(shortGrid.capacity()==1,"Small overview lost its complete fallback tile");
}
void keyboardUsesRowsAndPages(){
 auto grid=overviewLayout(720,630);
 require(overviewNavigate(0,20,grid,OverviewDirection::Right)==1,"Right arrow does not select the next grid column");
 require(overviewNavigate(0,20,grid,OverviewDirection::Down)==3,"Down arrow does not select the next grid row");
 require(overviewNavigate(2,20,grid,OverviewDirection::Right)==2,"Right arrow wraps across grid rows");
 require(overviewNavigate(0,20,grid,OverviewDirection::Left)==0,"Left arrow leaves the first column");
 require(overviewNavigate(1,20,grid,OverviewDirection::NextPage)==7,"Page Down does not retain the column on the next page");
 require(overviewNavigate(19,20,grid,OverviewDirection::Down)==19,"Down arrow leaves the final partial row");
 require(overviewNavigate(7,20,overviewLayout(100,180),OverviewDirection::NextPage)==7,"Tiny overview destroys keyboard selection");
 require(overviewNavigate(-1,0,grid,OverviewDirection::Down)==-1,"Empty overview selects a nonexistent window");
}
void fullscreenGridAndEasing(){
 for(auto [width,height]:std::array<std::pair<float,float>,4>{{{2293.f,960.f},{1920.f,1080.f},{853.f,480.f},{320.f,360.f}}})for(int count:{0,1,2,3,4,6,25}){
  const auto grid=fullscreenOverviewLayout(width,height,count);require(grid.capacity()>0&&grid.capacity()<=6,"Fullscreen overview exceeds the bounded large-preview page");
  for(int i=0;i<grid.capacity();++i){const auto cell=grid.cell(i);require(cell.left>=0&&cell.top>=172&&cell.right<=width&&cell.bottom<=height-40&&cell.right>cell.left&&cell.bottom>cell.top,"Fullscreen tile escapes usable monitor bounds");require(grid.hit((cell.left+cell.right)/2,(cell.top+cell.bottom)/2)==i,"Fullscreen hit regions drift from visual cells");}
 }
 const auto large=fullscreenOverviewLayout(2293,960,2).cell(0),old=overviewLayout(720,630).cell(0);
 require(large.right-large.left>2*(old.right-old.left),"Fullscreen previews are not substantially larger than popup thumbnails");
 require(fullscreenOverviewLayout(2293,960,2).columns==2&&fullscreenOverviewLayout(2293,960,3).rows==1,"Few-window overview does not use a spacious centered row");
 require(overviewEase(0)==0&&overviewEase(1)==1&&overviewEase(.5f)>.5f,"Overview easing endpoints or ease-out response are invalid");
}
struct PreviewFixtures {
 HWND owner{};std::vector<win::Window> sources;
 PreviewFixtures(){owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Aether overview thumbnail owner",WS_POPUP|WS_VISIBLE,50,70,1400,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(owner!=nullptr,"Could not create thumbnail owner");
  for(int i=0;i<20;++i){auto h=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"Aether overview thumbnail source",WS_POPUP|WS_VISIBLE,70,90,640+i,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(h!=nullptr,"Could not create thumbnail source");DWORD pid=0;GetWindowThreadProcessId(h,&pid);sources.push_back({h,pid,L"Native source",L"",L"",L""});}pump();DwmFlush();
 }
 ~PreviewFixtures(){for(auto& source:sources)DestroyWindow(source.hwnd);DestroyWindow(owner);}
};
void nativePageOwnsOnlyVisibleRegistrations(){
 PreviewFixtures fixtures;ui::OverviewPreviews previews;auto grid=overviewLayout(1280,860);
 previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);
 std::vector<HTHUMBNAIL> handles;const int expectedWidths[]={640,641,642,643,644,645,646,647,648,649,650,651};
 for(int i=0;i<12;++i){auto& tile=previews.at(i);require(tile.state()==win::PreviewState::Ready,"Visible page tile has no live native thumbnail");handles.push_back(tile.nativeHandle());SIZE size{};require(SUCCEEDED(DwmQueryThumbnailSourceSize(tile.nativeHandle(),&size))&&size.cx==expectedWidths[i]&&size.cy==360,"Tile registration previews the wrong source");}
 auto first=previews.at(0).nativeHandle();previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);require(previews.at(0).nativeHandle()==first,"Unchanged page needlessly replaces DWM registrations");
 previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true,64);require(previews.at(0).nativeHandle()==first,"Opacity animation recreates native registrations");previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true,255);
 previews.sync(fixtures.owner,fixtures.sources,grid,12,1.f,true);
 SIZE changed{};require(SUCCEEDED(DwmQueryThumbnailSourceSize(previews.at(0).nativeHandle(),&changed))&&changed.cx==652,"Changed page kept the first page source");
 for(int i=8;i<12;++i)require(previews.at(i).nativeHandle()==nullptr,"Partial final page keeps off-page registrations");
 for(auto handle:handles){SIZE size{};require(FAILED(DwmQueryThumbnailSourceSize(handle,&size)),"Page change retained an off-page DWM registration");}
 previews.hide();for(int i=0;i<12;++i)require(previews.at(i).nativeHandle()==nullptr,"Closing overview retained native registrations");
}
void nativeFallbacksAndDisableReleaseOwnership(){
 PreviewFixtures fixtures;ui::OverviewPreviews previews;auto grid=overviewLayout(720,630);
 previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);auto first=previews.at(0).nativeHandle();require(first!=nullptr,"Fallback fixture did not register");
 ShowWindow(fixtures.sources[0].hwnd,SW_SHOWMINNOACTIVE);pump();previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);
 require(previews.at(0).state()==win::PreviewState::Ready&&previews.at(0).nativeHandle()==first,"Minimized tile lost its retained native preview");SIZE size{};require(SUCCEEDED(DwmQueryThumbnailSourceSize(first,&size)),"Minimizing invalidated the retained native thumbnail");
 require(IsIconic(fixtures.sources[0].hwnd),"Previewing a minimized tile restored its source");
 auto next=previews.at(1).nativeHandle();previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,false);
 for(int i=0;i<12;++i)require(previews.at(i).nativeHandle()==nullptr,"Disabled previews still own a live thumbnail");require(FAILED(DwmQueryThumbnailSourceSize(next,&size)),"Disabling previews retained native thumbnail ownership");
 previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);ShowWindow(fixtures.owner,SW_HIDE);previews.sync(fixtures.owner,fixtures.sources,grid,0,1.f,true);
 for(int i=0;i<12;++i)require(previews.at(i).nativeHandle()==nullptr,"Hidden overview retained thumbnail ownership");
}
LRESULT CALLBACK fixtureProcedure(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
struct NativeSession {
 std::filesystem::path directory;std::vector<HWND> sources;win::Handle process,thread;DWORD pid{};HWND controller{},activities{},edit{};
 explicit NativeSession(const std::wstring& executable,bool previews){try {
  require(!FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"),"Existing controller would interfere with isolated overview test");
  directory=std::filesystem::temp_directory_path()/(L"AetherOverview-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  std::filesystem::create_directories(directory);win::setConfigDirectory(directory);Config config;config.hideTaskbar=false;config.windowPreviews=previews;win::saveConfig(config);win::setConfigDirectory({});
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProcedure;wc.lpszClassName=L"Aether.Overview.Fixture";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH));RegisterClassW(&wc);
  for(int i=0;i<20;++i){std::wostringstream title;title<<L"AetherOverviewFixture "<<std::setfill(L'0')<<std::setw(2)<<i;auto h=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,title.str().c_str(),WS_OVERLAPPEDWINDOW,50,70,600,350,nullptr,nullptr,wc.hInstance,nullptr);require(h!=nullptr,"Could not create native overview source");sources.push_back(h);ShowWindow(h,SW_SHOWMINNOACTIVE);}
  std::wstring command=L"\""+executable+L"\" --settings-dir \""+directory.wstring()+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION info{};
  require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)!=FALSE,"Could not launch isolated Aether process");process.reset(info.hProcess);thread.reset(info.hThread);pid=info.dwProcessId;
  require(waitFor([&]{controller=owned(pid,L"Aether UI Controller");return controller!=nullptr;},10000),"Isolated controller did not start");
 } catch(...){cleanup();throw;}}
 ~NativeSession(){cleanup();}
 void cleanup(){if(controller)PostMessageW(controller,WM_CLOSE,0,0);if(process)waitFor([&]{return WaitForSingleObject(process.value,0)==WAIT_OBJECT_0;},10000);for(auto h:sources)DestroyWindow(h);win::setConfigDirectory({});std::error_code e;std::filesystem::remove_all(directory,e);}
 LPARAM at(float x,float y){auto dpi=win::dpiFor(activities);return MAKELPARAM(static_cast<int>(x*dpi/96.f),static_cast<int>(y*dpi/96.f));}
 OverviewGrid grid(){RECT r{};GetClientRect(activities,&r);const float scale=win::dpiFor(activities)/96.f;return workspaceOverviewLayout(static_cast<float>(r.right)/scale,static_cast<float>(r.bottom)/scale,20);}
 void open(){
  // SW_MINIMIZE relinquishes a previously activated fixture's foreground state.
  // Do not acquire foreground focus: Windows may deny it while the user works.
  for(auto h:sources)ShowWindow(h,SW_MINIMIZE);
  require(waitFor([&]{
   const auto foreground=GetForegroundWindow();
   return std::all_of(sources.begin(),sources.end(),[&](HWND h){return IsIconic(h)&&h!=foreground;});
  },2000),"Owned sources are not all minimized and inactive; fixture ordering is not established");
  PostMessageW(controller,win::RefreshMessage,0,0);
  // Refresh is intentionally coalesced; give the event one bounded pump before opening.
  auto until=GetTickCount64()+180;while(GetTickCount64()<until){pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}
  PostMessageW(controller,WM_HOTKEY,1,0);require(waitFor([&]{activities=owned(pid,L"Aether Activities");return activities&&IsWindowVisible(activities);}),"Activities did not open");
  edit=GetDlgItem(activities,10);require(edit!=nullptr,"Overview search edit is missing");
  RECT bounds{};GetWindowRect(activities,&bounds);auto dpi=win::dpiFor(activities);SetWindowPos(activities,nullptr,bounds.left,bounds.top,MulDiv(720,dpi,96),MulDiv(630,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE);
  PostMessageW(activities,WM_LBUTTONUP,0,at(220,120));require(waitFor([&]{return GetPropW(activities,L"Aether.Overview.Fullscreen")&&!GetPropW(activities,L"Aether.Overview.Animating");}),"Posted Open windows transition did not settle");SendMessageW(edit,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"AetherOverviewFixture"));
  require(waitFor([&]{return IsWindowVisible(activities)&&!GetPropW(activities,L"Aether.Overview.Animating");}),"Fullscreen transition does not settle visibly before selection");
  // Tab from the Open windows tab to results; caret arrows remain native in search.
  SendMessageW(activities,WM_KEYDOWN,VK_TAB,0);
 }
 void key(WPARAM key){require(IsWindowVisible(activities),"Overview lost visibility before the requested keyboard action");SendMessageW(activities,WM_KEYDOWN,key,0);}
 void action(long id){win::ComPtr<IAccessible> client;require(SUCCEEDED(AccessibleObjectFromWindow(activities,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&client))),"Overview accessibility unavailable");VARIANT child{};child.vt=VT_I4;child.lVal=id;require(SUCCEEDED(client->accDoDefaultAction(child)),"Overview accessible control action failed");}
 RECT actionBounds(long id){win::ComPtr<IAccessible> client;require(SUCCEEDED(AccessibleObjectFromWindow(activities,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(&client))),"Overview accessibility unavailable");VARIANT child{};child.vt=VT_I4;child.lVal=id;long x=0,y=0,w=0,h=0;const auto hr=client->accLocation(&x,&y,&w,&h,child);if(FAILED(hr)||w<=0||h<=0){std::cerr<<"Workspace bounds id="<<id<<" hr="<<std::hex<<hr<<std::dec<<" workspaces="<<reinterpret_cast<ULONG_PTR>(GetPropW(activities,L"Aether.Overview.Workspaces"))<<" width="<<w<<" height="<<h<<"\n";}require(SUCCEEDED(hr)&&w>0&&h>0,"Workspace action bounds unavailable");POINT p{x,y};ScreenToClient(activities,&p);return {p.x,p.y,p.x+w,p.y+h};}
 void count(int expected){require(waitFor([&]{return reinterpret_cast<std::uintptr_t>(GetPropW(activities,L"Aether.Overview.Results"))==static_cast<std::uintptr_t>(expected)&&!GetPropW(activities,L"Aether.Overview.Animating");}),"Overview filtered/grouped result count differs");}
 void restored(int index,const char* step){
  const bool observed=waitFor([&]{return IsWindow(sources[static_cast<std::size_t>(index)])&&!IsIconic(sources[static_cast<std::size_t>(index)]);});
  if(!observed){
   wchar_t query[257]{};SendMessageW(edit,WM_GETTEXT,257,reinterpret_cast<LPARAM>(query));
   std::cerr<<"Overview step "<<step<<" expected source "<<index<<", popup visible="<<IsWindowVisible(activities)<<", query="<<win::utf8(query)<<", restored sources:";
   DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);std::cerr<<" foregroundPid="<<foregroundPid<<" AetherPid="<<pid<<" animation="<<GetPropW(activities,L"Aether.Overview.Animating");
   for(int i=0;i<20;++i)if(!IsIconic(sources[static_cast<std::size_t>(i)]))std::cerr<<' '<<i;std::cerr<<'\n';
   win::ComPtr<IVirtualDesktopManager> desktops;CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&desktops));
   for(int i=0;i<2;++i){const auto h=sources[static_cast<std::size_t>(i)];DWORD cloaked=0;BOOL current=TRUE;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(desktops)desktops->IsWindowOnCurrentVirtualDesktop(h,&current);std::cerr<<"Fixture "<<i<<": visible="<<IsWindowVisible(h)<<", iconic="<<IsIconic(h)<<", cloaked="<<cloaked<<", current desktop="<<current<<'\n';}
  }
  require(observed,"Overview activated a different tile or did not restore the selected source");
  for(int i=0;i<20;++i)if(i!=index)require(IsIconic(sources[static_cast<std::size_t>(i)])!=FALSE,"Overview restored an unselected source");
 }
};
void nativeGridActivation(const std::wstring& executable,bool previews){
 NativeSession session(executable,previews);session.open();session.key(VK_RIGHT);session.key(VK_RETURN);session.restored(1,"Right/Enter");
 session.open();const auto columns=session.grid().columns;session.key(VK_DOWN);session.key(VK_RETURN);session.restored(columns,"Down/Enter");
 session.open();const auto capacity=session.grid().capacity();session.key(VK_NEXT);session.key(VK_RETURN);session.restored(capacity,"PageDown/Enter");
 session.open();session.key(VK_END);session.key(VK_RETURN);session.restored(19,"End/Enter");
 session.open();session.key(VK_NEXT);session.key(VK_HOME);session.key(VK_RETURN);session.restored(0,"Home/Enter");
 session.open();auto first=session.grid().cell(0),second=session.grid().cell(1);SendMessageW(session.activities,WM_MOUSEMOVE,0,session.at((first.left+first.right)/2,(first.top+first.bottom)/2));SendMessageW(session.activities,WM_LBUTTONUP,0,session.at((second.left+second.right)/2,(second.top+second.bottom)/2));session.restored(1,"click current coordinates");
 session.open();session.key(VK_RIGHT);PostMessageW(session.controller,win::RefreshMessage,0,0);
 auto until=GetTickCount64()+180;while(GetTickCount64()<until){pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);}session.key(VK_RETURN);session.restored(1,"selection retention on snapshot");
 session.open();session.key(VK_ESCAPE);require(waitFor([&]{return !IsWindowVisible(session.activities);}),"Escape does not close the overview");
}
void nativeOverviewControls(const std::wstring& executable){
 NativeSession session(executable,true);session.open();session.count(20);
 session.action(21);session.count(1);session.key(VK_RETURN);session.count(20);session.key(VK_ESCAPE);session.count(1);session.action(21);session.count(20);
 const auto monitors=win::monitors();for(const auto& monitor:monitors){session.action(20);const int expected=static_cast<int>(std::count_if(session.sources.begin(),session.sources.end(),[&](HWND h){return MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST)==monitor.handle;}));session.count(expected);}session.action(20);session.count(20);
 auto cell=session.grid().cell(0);const float x=(cell.left+cell.right)/2,y=(cell.top+cell.bottom)/2;
 SendMessageW(session.activities,WM_LBUTTONDOWN,MK_LBUTTON,session.at(x,y));SendMessageW(session.activities,WM_MOUSEMOVE,MK_LBUTTON,session.at(x+20,y+20));session.key(VK_ESCAPE);
 for(auto source:session.sources)require(IsIconic(source)&&IsWindow(source),"Cancelled drag changed an owned source");require(IsWindowVisible(session.activities),"Cancelling drag dismissed the overview");
 auto pressedCell=session.grid().cell(1);const auto press=session.at((pressedCell.left+pressedCell.right)/2,(pressedCell.top+pressedCell.bottom)/2);POINT physicalPress{GET_X_LPARAM(press),GET_Y_LPARAM(press)};ClientToScreen(session.activities,&physicalPress);SetCursorPos(physicalPress.x,physicalPress.y);pump();Sleep(80);SendMessageW(session.activities,WM_LBUTTONDOWN,MK_LBUTTON,press);SendMessageW(session.activities,WM_LBUTTONUP,0,press);session.restored(1,"pressed tile identity");session.open();session.count(20);
 session.action(6000);require(waitFor([&]{return !IsWindow(session.sources[0]);}),"Close control did not request the selected source's normal close");
 for(std::size_t i=1;i<session.sources.size();++i)require(IsIconic(session.sources[i])&&IsWindow(session.sources[i]),"Close control touched another source");session.count(19);
 session.key(VK_ESCAPE);require(waitFor([&]{return !IsWindowVisible(session.activities);}),"Controls overview did not dismiss");
}
void nativeWorkspaceDragAndSwitch(const std::wstring& executable){
 win::Workspaces service;require(service.refresh()&&service.available(),"Native workspace actions unavailable on this build");const auto initial=service.items();const auto original=std::find_if(initial.begin(),initial.end(),[](const auto& item){return item.current;})->id;
 require(service.create(),"Temporary native workspace creation failed");const auto added=std::find_if(service.items().begin(),service.items().end(),[&](const auto& item){return std::none_of(initial.begin(),initial.end(),[&](const auto& old){return old.id==item.id;});});require(added!=service.items().end(),"New workspace identity unavailable");const auto target=added->id;const int index=static_cast<int>(std::distance(service.items().begin(),added));
 try{
  {
   NativeSession session(executable,true);session.open();session.count(20);win::ComPtr<IVirtualDesktopManager> verifier;require(SUCCEEDED(CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&verifier))),"Public workspace verifier unavailable");
   const auto width=session.grid().width;const int perPage=std::clamp(static_cast<int>((width-180.f)/150.f),1,6);for(int page=0;page<index/perPage;++page)session.action(31);
   auto targetBounds=session.actionBounds(4000+index);auto cell=session.grid().cell(0);const float x=(cell.left+cell.right)/2.f,y=(cell.top+cell.bottom)/2.f;const int targetX=(targetBounds.left+targetBounds.right)/2,targetY=(targetBounds.top+targetBounds.bottom)/2;
   POINT start{GET_X_LPARAM(session.at(x,y)),GET_Y_LPARAM(session.at(x,y))},end{targetX,targetY};ClientToScreen(session.activities,&start);ClientToScreen(session.activities,&end);SetCursorPos(start.x,start.y);Sleep(80);INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;require(SendInput(1,&input,sizeof(input))==1,"Owned drag down unavailable");Sleep(80);SetCursorPos(end.x,end.y);Sleep(100);input.mi.dwFlags=MOUSEEVENTF_LEFTUP;require(SendInput(1,&input,sizeof(input))==1,"Owned drag up unavailable");
   GUID actual{};require(waitFor([&]{return SUCCEEDED(verifier->GetWindowDesktopId(session.sources[0],&actual))&&actual==target;}),"Dropping onto a workspace did not move the selected foreign source");require(IsIconic(session.sources[0]),"Workspace drop restored a minimized source");
   for(std::size_t i=1;i<session.sources.size();++i){require(SUCCEEDED(verifier->GetWindowDesktopId(session.sources[i],&actual))&&actual==original,"Workspace drop moved another source");}
   require(service.move({session.sources[0],GetCurrentProcessId(),L"Owned drag source",L"",L"",L""},original),"Dragged source could not be returned to its original workspace");
   session.count(20);session.action(4000+index);require(waitFor([&]{service.refresh();return std::any_of(service.items().begin(),service.items().end(),[&](const auto& item){return item.id==target&&item.current;});}),"Workspace strip did not switch Windows desktops");require(!IsWindowVisible(session.activities),"Workspace switch kept an obsolete overview visible");
   require(service.switchTo(original),"Could not return to the original Windows desktop");require(waitFor([&]{service.refresh();return std::any_of(service.items().begin(),service.items().end(),[&](const auto& item){return item.id==original&&item.current;});}),"Original Windows desktop not restored");
  }
  require(service.removeCreated(target,original),"Owned temporary desktop cleanup failed");require(service.items().size()==initial.size(),"Workspace test changed the final desktop count");
 }catch(...){service.switchTo(original);service.removeCreated(target,original);throw;}
}
}
int main(int argc,char** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment apartment;
 if(argc==3&&std::string_view(argv[1])=="--workspace-actions"){try{nativeWorkspaceDragAndSwitch(win::wide(argv[2]));std::cout<<"PASS actual overview drag, source isolation, desktop switching and exact workspace cleanup\n";return 0;}catch(const std::exception& error){std::cerr<<"FAIL actual overview workspace actions: "<<error.what()<<'\n';return 1;}}
 std::vector<std::pair<const char*,std::function<void()>>> tests={{"fullscreen grid and easing",fullscreenGridAndEasing},{"grid bounds and paging",gridBoundsAndPaging},{"keyboard uses rows and pages",keyboardUsesRowsAndPages},{"native page owns only visible registrations",nativePageOwnsOnlyVisibleRegistrations},{"native fallbacks and disable release ownership",nativeFallbacksAndDisableReleaseOwnership}};
 std::wstring executable=argc==2?win::wide(argv[1]):L"";if(argc==2){tests.push_back({"native grid activation with previews",[&]{nativeGridActivation(executable,true);}});tests.push_back({"native grid activation without previews",[&]{nativeGridActivation(executable,false);}});tests.push_back({"native monitor filters, grouping, close and drag cancellation",[&]{nativeOverviewControls(executable);}});}
 int failures=0;for(const auto& [name,test]:tests)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}return failures?1:0;
}










