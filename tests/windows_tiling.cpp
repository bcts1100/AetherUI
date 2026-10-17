#include "platform/tiling.hpp"
#include "platform/layout_library.hpp"
#include <iostream>
#include <stdexcept>
#include <future>
#include <thread>
using namespace aether;using namespace aether::win;
namespace {
int checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_GETMINMAXINFO){if(GetPropW(h,L"Aether.HungMin"))Sleep(500);auto* limits=reinterpret_cast<MINMAXINFO*>(l);limits->ptMinTrackSize={120,100};return 0;}
 return DefWindowProcW(h,m,w,l);
}
Rect frame(HWND h){RECT r{};if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r))))GetWindowRect(h,&r);return rect(r);}
LRESULT CALLBACK hungProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return fixtureProc(h,m,w,l);}
bool hungQueryBounded(const Monitor& monitor,const std::vector<Monitor>& displays){
 std::promise<HWND> promise;auto future=promise.get_future();std::thread worker([&]{WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=hungProc;wc.lpszClassName=L"Aether.Tiling.HungOwned";RegisterClassW(&wc);HWND hwnd=CreateWindowW(wc.lpszClassName,L"Owned hung tiling fixture",WS_OVERLAPPEDWINDOW,monitor.work.left+80,monitor.work.top+80,320,240,nullptr,nullptr,wc.hInstance,nullptr);if(hwnd){ShowWindow(hwnd,SW_SHOWNOACTIVATE);SetPropW(hwnd,L"Aether.HungMin",reinterpret_cast<HANDLE>(1));}promise.set_value(hwnd);if(hwnd){MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}});
 HWND hwnd=future.get();bool pass=false;if(hwnd){TilingService service(true);service.allowFixture(hwnd,GetCurrentProcessId());Config config;config.tilingDisplays[utf8(monitor.name)]={true,"balanced"};Window window;window.hwnd=hwnd;window.pid=GetCurrentProcessId();window.display=monitor.name;window.title=L"Owned hung tiling fixture";window.bounds=frame(hwnd);auto started=GetTickCount64();service.update(config,displays,{window});auto elapsed=GetTickCount64()-started;pass=elapsed<250&&service.statistics().moves==0&&service.statistics().failures==1;PostMessageW(hwnd,WM_CLOSE,0,0);}worker.join();return pass;
}
Window record(HWND h,const Monitor& monitor){Window w;w.hwnd=h;w.pid=GetCurrentProcessId();w.display=monitor.name;w.title=L"Owned tiling fixture";w.bounds=frame(h);w.minimized=IsIconic(h)!=FALSE;return w;}
}
int main(){std::vector<HWND> owned;std::filesystem::path profile;try{
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ComApartment com;
 auto displays=monitors();check(!displays.empty(),"monitor snapshot available");auto monitor=displays.front();
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProc;wc.lpszClassName=L"Aether.Tiling.Owned";RegisterClassW(&wc);
 for(int i=0;i<3;++i){auto h=CreateWindowExW(0,wc.lpszClassName,L"Owned tiling fixture",WS_OVERLAPPEDWINDOW,monitor.work.left+80+i*40,monitor.work.top+80+i*30,320,240,nullptr,nullptr,wc.hInstance,nullptr);check(h!=nullptr,"fixture creation");owned.push_back(h);ShowWindow(h,SW_SHOWNOACTIVATE);}
 TilingService service(true);for(auto h:owned)service.allowFixture(h,GetCurrentProcessId());
 auto snapshot=[&]{std::vector<Window> result;for(auto h:owned)result.push_back(record(h,monitor));return result;};
 Config config;auto initial=snapshot();service.update(config,displays,initial);check(service.statistics().moves==0,"auto disabled by default");
 config.tilingDisplays[utf8(monitor.name)]={true,"balanced"};auto foreground=GetForegroundWindow();
 service.update(config,displays,initial);check(service.statistics().moves==3,"enabled policy tiles owned fixtures");check(GetForegroundWindow()==foreground,"tiling retains foreground window");
 auto first=snapshot();for(auto w:first)check(w.bounds.left>=monitor.work.left&&w.bounds.top>=monitor.work.top&&w.bounds.right<=monitor.work.right&&w.bounds.bottom<=monitor.work.bottom,"DWM frame inside work area");
 auto moves=service.statistics().moves;for(int i=0;i<20;++i)service.update(config,displays,snapshot());check(service.statistics().moves==moves,"own geometry events and idle snapshots do not reflow");
 auto manual=owned.front();SetWindowPos(manual,nullptr,monitor.work.left+60,monitor.work.top+70,320,220,SWP_NOACTIVATE|SWP_NOZORDER);service.observeUserMove(manual);auto manualRect=frame(manual);
 service.update(config,displays,snapshot());check(frame(manual)==manualRect,"manual movement releases its window");
 service.update(config,displays,snapshot(),false,monitor.name);check(frame(manual)!=manualRect,"explicit tile now re-enrolls manual window");
 ShowWindow(owned[1],SW_MINIMIZE);service.update(config,displays,snapshot());check(IsIconic(owned[1])!=FALSE,"automatic tiling never restores minimized fixture");
 moves=service.statistics().moves;service.update(config,displays,snapshot(),true);check(service.statistics().moves==moves,"paused modal/session state suppresses movements");
 auto full=snapshot();full.front().fullscreen=true;full.front().bounds=monitor.bounds;service.update(config,displays,full,false,monitor.name);check(service.statistics().moves==moves,"fullscreen monitor suppresses explicit retile");
 EnableWindow(owned[2],FALSE);auto before=frame(owned[2]);service.reset();moves=service.statistics().moves;service.update(config,displays,snapshot());check(frame(owned[2])==before,"disabled modal-owner window is not tiled");check(service.statistics().moves==moves,"disabled modal owner suppresses the entire affected monitor");
 // Distinct synthetic monitor names/workareas exercise monitor policy without
 // changing physical displays; every mutation still targets whitelisted fixtures.
 SetWindowPos(owned[0],nullptr,monitor.work.left+80,monitor.work.top+90,320,240,SWP_NOZORDER|SWP_NOACTIVATE);auto unaffectedBefore=frame(owned[0]);
 Monitor other=monitor;other.name=L"Owned synthetic other display";other.work.right=other.work.left+other.work.width()/2;Config isolated=config;isolated.tilingDisplays[utf8(other.name)]={true,"balanced"};TilingService modalService(true);modalService.allowFixture(owned[0],GetCurrentProcessId());modalService.allowFixture(owned[2],GetCurrentProcessId());auto unaffected=record(owned[0],other);auto modalOwner=record(owned[2],monitor);modalService.update(isolated,{monitor,other},{modalOwner,unaffected});auto otherTarget=zoneRect(other.work,{0,0,10000,10000},8,other.dpi,120,100);check(modalService.statistics().moves==1&&modalService.statistics().suppressed==1&&frame(owned[2])==before&&frame(owned[0])!=unaffectedBefore&&otherTarget&&frame(owned[0])==*otherTarget,"modal suppression is per monitor and leaves another policy active");EnableWindow(owned[2],TRUE);
 config.snapLayouts={{"custom-1","Fixture zones",{{0,0,5000,10000},{5000,0,10000,10000}},8}};config.tilingDisplays[utf8(monitor.name)].layoutId="custom-1";
 check(service.snapWindow(record(owned[2],monitor),config,displays,1,true),"custom zone command uses native boundary");auto customRect=zoneRect(monitor.work,config.snapLayouts.front().zones[1],8,monitor.dpi,120,100);check(customRect&&frame(owned[2])==*customRect,"custom snap DWM geometry");
 ShowWindow(owned[2],SW_SHOWMAXIMIZED);check(IsZoomed(owned[2])!=FALSE,"owned fixture maximized for legacy snap");
 check(service.snapWindow(record(owned[2],monitor),config,displays,0,false)&&!IsZoomed(owned[2]),"explicit legacy snap restores maximized fixture");
 ShowWindow(owned[2],SW_MINIMIZE);check(!service.snapWindow(record(owned[2],monitor),config,displays,0,false)&&IsIconic(owned[2]),"explicit command does not restore minimized fixture");ShowWindow(owned[2],SW_SHOWNOACTIVATE);
 ShowWindow(owned[2],SW_SHOWMAXIMIZED);check(!service.moveWindow(record(owned[2],monitor),{monitor},MoveDirection::Left)&&IsZoomed(owned[2]),"no neighbor movement preserves maximized state");ShowWindow(owned[2],SW_SHOWNOACTIVATE);DWORD_PTR restored=0;SendMessageTimeoutW(owned[2],WM_SYSCOMMAND,SC_RESTORE,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,40,&restored);
 std::vector<Rect> areas;for(const auto& display:displays)areas.push_back(display.bounds);std::optional<std::size_t> neighbor;MoveDirection direction=MoveDirection::Right;
 for(auto candidate:{MoveDirection::Left,MoveDirection::Right,MoveDirection::Up,MoveDirection::Down}){neighbor=adjacentMonitorIndex(areas,0,candidate);if(neighbor){direction=candidate;break;}}
 if(neighbor){check(service.moveWindow(record(owned[2],monitor),displays,direction),"owned fixture native monitor movement");auto movedFrame=frame(owned[2]);const auto& target=displays[*neighbor];check(MonitorFromWindow(owned[2],MONITOR_DEFAULTTONEAREST)==target.handle&&movedFrame.left>=target.work.left&&movedFrame.top>=target.work.top&&movedFrame.right<=target.work.right&&movedFrame.bottom<=target.work.bottom,"moved owned frame maps inside actual adjacent monitor work area");}
 else std::cout<<"SKIP physical adjacent-monitor move: only one available display; pure mixed-DPI transfer tests remain active\n";
 TilingService whitelist(true);whitelist.allowFixture(owned[0],GetCurrentProcessId()+1);auto beforeWrong=frame(owned[0]);whitelist.update(config,displays,snapshot());check(whitelist.statistics().moves==0&&frame(owned[0])==beforeWrong,"PID whitelist rejects unrelated HWND");
 check(hungQueryBounded(monitor,displays),"hung minimum-size query times out without movement");
 profile=executablePath().parent_path()/L"test-profiles"/(L"tiling-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));setConfigDirectory(profile);
 Config persisted;persisted.defaults.clock=false;persisted.defaults.status=false;persisted.snapLayouts={{"custom-1","My zones",{{0,0,5000,10000},{5000,0,10000,10000}},9}};persisted.tilingDefaults={true,"custom-1"};persisted.tilingDisplays["portrait"]={false,"master"};
 std::string error;auto saved=saveLayoutPreset(persisted,"Tiled desk",&error);check(saved.has_value(),"v2 tiling layout save");auto loaded=loadLayoutPreset(saved->id,&error);check(loaded&&loaded->snapLayouts==persisted.snapLayouts&&loaded->tilingDefaults==persisted.tilingDefaults&&loaded->tilingDisplays==persisted.tilingDisplays,"v2 full tiling profile roundtrip");
 Config current;current.tilingDisplays["disconnected"]={false,"master"};auto applied=applyLayoutPreset(current,*loaded,{"portrait"},&error);check(applied.snapLayouts==persisted.snapLayouts&&applied.tilingForDisplay("portrait")==persisted.tilingForDisplay("portrait")&&applied.tilingDisplays.contains("disconnected"),"v2 applying retains disconnected preferences");
 auto file=profile/L"layout-presets"/(wide(saved->id)+L".aetherlayout");auto text=readText(file);check(loaded&&loaded->defaults==persisted.defaults,"saved layout retains customized top-bar visibility");
 writeAtomic(file,"[AetherLayout]\nversion=1\n[preset]\nname=Legacy\n[layout]\nedge=bottom\niconSize=48\ndockGap=10\ntopBar=1\ndock=1\nautoHide=0\nmonitorOnly=1\nreserveDock=1\n");auto old=loadLayoutPreset(saved->id,&error);check(old&&!old->tilingDefaults.enabled&&old->snapLayouts.empty()&&old->defaults.clock&&old->defaults.focusedTitle&&old->defaults.status,"v1 layout backward compatibility retains default visibility and tiling off");
 writeAtomic(file,text+"\n[unsafe]\ncommand=bad\n");check(!loadLayoutPreset(saved->id,&error),"strict v2 refuses unexpected sections");
 for(auto h:owned)DestroyWindow(h);setConfigDirectory({});std::error_code ec;std::filesystem::remove_all(profile,ec);std::cout<<checks<<" native tiling checks passed\n";return 0;
 }catch(const std::exception& e){for(auto h:owned)if(IsWindow(h))DestroyWindow(h);setConfigDirectory({});if(!profile.empty()){std::error_code ec;std::filesystem::remove_all(profile,ec);}std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
