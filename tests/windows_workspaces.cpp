#include "platform/workspaces.hpp"
#include "core/overview.hpp"
#include <iostream>
#include <functional>
using namespace aether;
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
bool waitFor(const std::function<bool()>& test){auto end=GetTickCount64()+4000;do{pump();if(test())return true;Sleep(10);}while(GetTickCount64()<end);return false;}
struct Window {
 HWND hwnd{};int closes=0;
 static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){auto* self=reinterpret_cast<Window*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(m==WM_NCCREATE){self=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(m==WM_CLOSE&&self){++self->closes;return 0;}return DefWindowProcW(h,m,w,l);}
 Window(){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=procedure;c.lpszClassName=L"Aether.Workspace.OwnedFixture";RegisterClassW(&c);hwnd=CreateWindowExW(WS_EX_APPWINDOW,c.lpszClassName,L"Owned workspace fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,500,300,nullptr,nullptr,c.hInstance,this);require(hwnd!=nullptr,"Owned fixture unavailable");UpdateWindow(hwnd);pump();}
 ~Window(){if(hwnd)DestroyWindow(hwnd);}
 win::Window record()const{return {hwnd,GetCurrentProcessId(),L"Owned workspace fixture",win::executablePath().wstring(),L"fixture",L""};}
};
void closeRequestsAreNormalAndIdentityChecked(){Window window;auto source=window.record();require(win::requestWindowClose(source),"Normal close request failed");require(waitFor([&]{return window.closes==1;}),"Normal close did not reach WM_CLOSE");require(IsWindow(window.hwnd),"Close request bypassed the application's unsaved-work veto");auto wrong=source;++wrong.pid;require(!win::requestWindowClose(wrong),"Wrong source PID accepted");wrong=source;wrong.processBirth=1;require(!win::requestWindowClose(wrong),"Wrong source birth accepted");DestroyWindow(window.hwnd);window.hwnd=nullptr;require(!win::requestWindowClose(source),"Closed source accepted");}
void groupingAndLayout(){win::Window a{},b{};a.hwnd=reinterpret_cast<HWND>(1);b.hwnd=reinterpret_cast<HWND>(2);require(win::overviewAppKey(a)!=win::overviewAppKey(b),"Unknown apps were grouped together");a.path=L"C:\\Apps\\Tea.EXE";b.path=L"c:/apps/tea.exe";require(win::overviewAppKey(a)==win::overviewAppKey(b),"Path casing or separators split an app group");a.path=b.path=L"C:\\Windows\\ApplicationFrameHost.exe";a.identity.clear();b.identity.clear();require(win::overviewAppKey(a)!=win::overviewAppKey(b),"Unresolved shared-host apps were merged");a.identity=L"aumid:one";b.identity=L"aumid:two";require(win::overviewAppKey(a)!=win::overviewAppKey(b),"Distinct packaged apps merged");
 for(auto size:{SIZE{2293,960},SIZE{1280,720},SIZE{853,480},SIZE{320,360}})for(int count:{0,1,2,4,6,20}){auto grid=workspaceOverviewLayout(static_cast<float>(size.cx),static_cast<float>(size.cy),count);require(grid.capacity()>0&&grid.capacity()<=6,"Workspace grid capacity invalid");for(int i=0;i<grid.capacity();++i){auto c=grid.cell(i);require(c.left>=0&&c.right<=size.cx&&c.top>=172&&c.bottom<=size.cy-74&&c.bottom>c.top+60,"Workspace chrome overlaps the preview grid");require(grid.hit((c.left+c.right)/2,(c.top+c.bottom)/2)==i,"Workspace grid hit mismatch");}}
}
void passiveSnapshot(){win::Workspaces service;const bool available=service.refresh();if(!available){require(!service.notice().empty(),"Unsupported workspace service has no explanation");std::cout<<"INFO native workspace controls unavailable on this Windows build\n";return;}require(!service.items().empty()&&service.items().size()<=128,"Workspace enumeration unbounded");require(std::count_if(service.items().begin(),service.items().end(),[](const auto& w){return w.current;})==1,"Current workspace is ambiguous");require(!service.switchTo(GUID_NULL),"Invalid workspace accepted");require(!service.removeCreated(service.items().front().id,GUID_NULL),"Unowned workspace removal accepted");}
void ownedNativeActions(bool hold){
 win::Workspaces service;require(service.refresh()&&service.available(),"Native workspace service unavailable for action acceptance");const auto initial=service.items();const auto original=std::find_if(initial.begin(),initial.end(),[](const auto& w){return w.current;})->id;
 require(service.create(),"Workspace creation failed");auto created=std::find_if(service.items().begin(),service.items().end(),[&](const auto& w){return std::none_of(initial.begin(),initial.end(),[&](const auto& old){return old.id==w.id;});});require(created!=service.items().end(),"Created workspace identity unavailable");const auto target=created->id;
 Window window;win::ComPtr<IVirtualDesktopManager> manager;require(SUCCEEDED(CoCreateInstance(CLSID_VirtualDesktopManager,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&manager))),"Public desktop verifier unavailable");
 try{
  GUID id{};require(waitFor([&]{return SUCCEEDED(manager->GetWindowDesktopId(window.hwnd,&id))&&id==original;}),"Owned window not admitted by Windows");ShowWindow(window.hwnd,SW_SHOWMINNOACTIVE);pump();
  require(waitFor([&]{return service.move(window.record(),target);}),"Native window move failed");require(SUCCEEDED(manager->GetWindowDesktopId(window.hwnd,&id))&&id==target&&IsIconic(window.hwnd),"Move did not preserve minimized state or target identity");
  require(service.move(window.record(),original),"Native window move back failed");require(service.switchTo(target),"Native workspace switch failed");require(waitFor([&]{service.refresh();return std::any_of(service.items().begin(),service.items().end(),[&](const auto& w){return w.id==target&&w.current;});}),"Windows did not switch to created workspace");
  if(hold){auto end=GetTickCount64()+2000;while(GetTickCount64()<end){pump();Sleep(20);}}
  require(service.switchTo(original),"Original workspace switch back failed");require(waitFor([&]{service.refresh();return std::any_of(service.items().begin(),service.items().end(),[&](const auto& w){return w.id==original&&w.current;});}),"Original workspace not restored");
  require(service.removeCreated(target,original),"Owned temporary workspace removal failed");require(service.items().size()==initial.size(),"Workspace count not restored");
 }catch(...){service.move(window.record(),original);service.switchTo(original);service.removeCreated(target,original);throw;}
}
}
int main(int argc,char** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);win::ComApartment apartment;try{closeRequestsAreNormalAndIdentityChecked();std::cout<<"PASS normal close, application veto and stale identity guards\n";groupingAndLayout();std::cout<<"PASS grouping keys and workspace layout bounds\n";passiveSnapshot();std::cout<<"PASS native workspace snapshot and invalid operations\n";if(argc==2&&(std::string_view(argv[1])=="--actions"||std::string_view(argv[1])=="--actions-hold")){ownedNativeActions(std::string_view(argv[1])=="--actions-hold");std::cout<<"PASS native workspace create, minimized move, switch, restore and owned removal\n";}return 0;}catch(const std::exception& e){std::cerr<<"FAIL workspace controls: "<<e.what()<<'\n';return 1;}}


