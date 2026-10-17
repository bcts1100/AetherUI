#include "platform/provider_codec.hpp"
#include "platform/display_policy.hpp"
#include "platform/config.hpp"
#include <propkey.h>
#include <propvarutil.h>
#include <iostream>
#include <chrono>
#include <set>
int failures=0,checks=0;
void check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;}
LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+90||m==WM_APP+91){
  aether::win::ComPtr<IPropertyStore> store;HRESULT result=SHGetPropertyStoreForWindow(h,IID_PPV_ARGS(&store));
  if(SUCCEEDED(result)){
   PROPVARIANT value{};result=InitPropVariantFromString(m==WM_APP+90?L"Aether.Fixture.Two":L"Aether.Fixture.Three",&value);
   if(SUCCEEDED(result))result=store->SetValue(PKEY_AppUserModel_ID,value);if(SUCCEEDED(result))result=store->Commit();PropVariantClear(&value);
  }
  if(SUCCEEDED(result)){
   if(m==WM_APP+91){NotifyWinEvent(EVENT_OBJECT_DESTROY,h,OBJID_WINDOW,CHILDID_SELF);NotifyWinEvent(EVENT_OBJECT_SHOW,h,OBJID_WINDOW,CHILDID_SELF);}
   else NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,h,OBJID_WINDOW,CHILDID_SELF);
  }
  return SUCCEEDED(result)?1:0;
 }
 if(m==WM_APP+93||m==WM_APP+94){NotifyWinEvent(m==WM_APP+93?EVENT_SYSTEM_MOVESIZESTART:EVENT_SYSTEM_MOVESIZEEND,h,OBJID_WINDOW,CHILDID_SELF);return 1;}
 if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
namespace {
int refreshMessages=0;
LRESULT CALLBACK receiverProc(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==aether::win::RefreshMessage){++refreshMessages;return 0;}
 return DefWindowProcW(h,message,w,l);
}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void pumpFor(ULONGLONG milliseconds){
 auto deadline=GetTickCount64()+milliseconds;
 do{pump();if(GetTickCount64()>=deadline)break;MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}while(GetTickCount64()<deadline);
}
void cacheDiagnostics(const char* phase,const aether::win::WindowScanStats& before,const aether::win::WindowScanStats& after,
 const std::vector<aether::win::Window>& first,const std::vector<aether::win::Window>& second){
 std::cout<<"DIAG "<<phase<<" counts before/after: path="<<before.pathQueries<<'/'<<after.pathQueries
  <<", identity="<<before.identityQueries<<'/'<<after.identityQueries<<", pathHits="<<before.pathCacheHits<<'/'<<after.pathCacheHits
  <<", identityHits="<<before.identityCacheHits<<'/'<<after.identityCacheHits<<", snapshots="<<before.snapshots<<'/'<<after.snapshots<<'\n';
 std::cout<<"DIAG "<<phase<<" deltas: path="<<after.pathQueries-before.pathQueries<<", identity="<<after.identityQueries-before.identityQueries
  <<", pathHits="<<after.pathCacheHits-before.pathCacheHits<<", identityHits="<<after.identityCacheHits-before.identityCacheHits
  <<", window counts="<<first.size()<<'/'<<second.size()<<'\n';
 using Identity=std::pair<std::uintptr_t,DWORD>;std::set<Identity> firstIds,secondIds;std::set<DWORD> pids;
 for(const auto& window:first){firstIds.emplace(reinterpret_cast<std::uintptr_t>(window.hwnd),window.pid);pids.insert(window.pid);}
 for(const auto& window:second){secondIds.emplace(reinterpret_cast<std::uintptr_t>(window.hwnd),window.pid);pids.insert(window.pid);}
 for(const auto& [hwnd,pid]:firstIds)if(!secondIds.contains({hwnd,pid}))std::cout<<"DIAG "<<phase<<" missing HWND=0x"<<std::hex<<hwnd<<std::dec<<" PID="<<pid<<'\n';
 for(const auto& [hwnd,pid]:secondIds)if(!firstIds.contains({hwnd,pid}))std::cout<<"DIAG "<<phase<<" extra HWND=0x"<<std::hex<<hwnd<<std::dec<<" PID="<<pid<<'\n';
 for(auto pid:pids){
  aether::win::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
  if(!process)std::cout<<"DIAG "<<phase<<" OpenProcess query/synchronize denied PID="<<pid<<" error="<<GetLastError()<<'\n';
  else if(WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT)std::cout<<"DIAG "<<phase<<" PID="<<pid<<" is no longer a live process\n";
 }
}
std::wstring identityOf(const std::vector<aether::win::Window>& snapshot,HWND hwnd){
 auto item=std::find_if(snapshot.begin(),snapshot.end(),[&](const auto& window){return window.hwnd==hwnd;});
 return item==snapshot.end()?std::wstring{}:item->identity;
}
}
std::vector<aether::win::Window> enriched(aether::win::WindowCollector& collector,bool reconcile=false){
 using namespace aether::win;collector.snapshot(reconcile);ProviderClient provider;
 for(;;){auto requests=collector.takeMetadataRequests();if(requests.empty())break;auto reply=provider.call(ProviderOperation::WindowMetadata,encodeWindowMetadataRequests(requests),nullptr,4000);
  if(reply.outcome==ProviderOutcome::Ok){auto metadata=decodeWindowMetadata(reply.bytes);if(metadata){collector.acceptMetadata(*metadata);continue;}}
  std::vector<WindowMetadata> failed;for(const auto& token:requests)failed.push_back(WindowMetadata{token,{}});collector.acceptMetadata(failed);
 }return collector.snapshot();
}int main(int argc,char**){
 using namespace aether;using namespace aether::win;
 ComApartment com;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc>1){WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProc;wc.lpszClassName=L"Aether.Policy.Fixture";RegisterClassW(&wc);HWND h=CreateWindowW(wc.lpszClassName,L"Aether policy fixture",WS_OVERLAPPEDWINDOW,40,100,320,200,nullptr,nullptr,wc.hInstance,nullptr);ComPtr<IPropertyStore> props;PROPVARIANT v{};if(SUCCEEDED(SHGetPropertyStoreForWindow(h,IID_PPV_ARGS(&props)))){InitPropVariantFromString(L"Aether.Fixture.One",&v);props->SetValue(PKEY_AppUserModel_ID,v);props->Commit();PropVariantClear(&v);}ShowWindow(h,SW_SHOWNOACTIVATE);HWND second=CreateWindowW(wc.lpszClassName,L"Aether policy fixture second",WS_OVERLAPPEDWINDOW,400,100,320,200,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(second,SW_SHOWNOACTIVATE);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;}
 Monitor left{nullptr,L"left",{-1920,0,0,1080},{-1920,0,0,1040},96,true};Monitor right{nullptr,L"right",{0,0,1920,1080},{0,0,1920,1040},144,false};
 Window span;span.bounds={-1920,0,1920,1080};span.display=L"left";span.fullscreen=true;
 auto both=fullscreenDisplays({span},{left,right});check(both==std::set<std::wstring>{L"left",L"right"},"spanning fullscreen suppresses every covered display");
 Window front;front.bounds={-1800,50,-100,800};front.display=L"left";
 check(fullscreenDisplays({front,span},{left,right})==std::set<std::wstring>{L"right"},"foreground partial window exposes only its display panels");
 span.minimized=true;check(fullscreenDisplays({span},{left,right}).empty(),"minimized fullscreen window cannot suppress panels");span.minimized=false;
 Window maximized;maximized.bounds=left.work;check(fullscreenDisplays({maximized},{left}).empty(),"maximized work area is not fullscreen");
 Window fullSizedNormal;fullSizedNormal.bounds=left.bounds;fullSizedNormal.display=L"left";fullSizedNormal.fullscreen=false;
 check(fullscreenDisplays({fullSizedNormal},{left}).empty(),"explicit nonfullscreen window covering the whole monitor does not suppress its panels");
 check(fullscreenDisplays({fullSizedNormal,span},{left,right})==std::set<std::wstring>{L"right"},"whole-monitor nonfullscreen front window occludes a fullscreen window behind it only on its own display");
 Monitor noReservation=left;noReservation.work=noReservation.bounds;
 check(fullscreenDisplays({fullSizedNormal},{noReservation}).empty(),"work area equal to monitor bounds does not override an explicit nonfullscreen classification");
 auto changed=right;changed.dpi=192;check(!sameDisplayTopology({right},{changed}),"DPI change requires topology handling");changed=right;changed.bounds.left=100;check(!sameDisplayTopology({right},{changed}),"geometry change requires topology handling");changed=right;changed.work.bottom-=20;check(sameDisplayTopology({right},{changed}),"appbar work-area change is not physical topology change");
 auto command=L"\""+executablePath().wstring()+L"\" --fixture";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){std::cerr<<"Fixture creation failed\n";return 2;}Handle process(pi.hProcess),thread(pi.hThread);HWND fixture=nullptr,secondFixture=nullptr;auto deadline=GetTickCount64()+5000;
 do{
  fixture=FindWindowW(L"Aether.Policy.Fixture",L"Aether policy fixture");secondFixture=FindWindowW(L"Aether.Policy.Fixture",L"Aether policy fixture second");
  DWORD primaryPid=0,secondaryPid=0;if(fixture)GetWindowThreadProcessId(fixture,&primaryPid);if(secondFixture)GetWindowThreadProcessId(secondFixture,&secondaryPid);
  if(primaryPid!=pi.dwProcessId)fixture=nullptr;if(secondaryPid!=pi.dwProcessId)secondFixture=nullptr;
  if(fixture&&secondFixture&&IsWindowVisible(fixture)&&IsWindowVisible(secondFixture))break;pumpFor(20);
 }while(GetTickCount64()<deadline);
 check(fixture&&secondFixture&&IsWindowVisible(fixture)&&IsWindowVisible(secondFixture),"both owned fixture windows are visible before cache warm-up");
 WindowCollector collector;auto first=enriched(collector);auto before=collector.statistics();auto second=collector.snapshot();auto after=collector.statistics();if(after.pathQueries!=before.pathQueries||after.identityQueries!=before.identityQueries)cacheDiagnostics("unchanged snapshots",before,after,first,second);
 check(fixture&&std::any_of(first.begin(),first.end(),[&](const Window& w){return w.hwnd==fixture;}),"collector discovers a real foreign process window");
 check(after.pathQueries==before.pathQueries,"unchanged snapshots reuse live process paths");check(after.identityQueries==before.identityQueries,"unchanged snapshots reuse Shell window identities");
  
 WNDCLASSW receiverClass{};receiverClass.hInstance=GetModuleHandleW(nullptr);receiverClass.lpfnWndProc=receiverProc;receiverClass.lpszClassName=L"Aether.Policy.EventsReceiver";RegisterClassW(&receiverClass);
 HWND receiver=CreateWindowW(receiverClass.lpszClassName,L"Policy event receiver",0,0,0,0,0,HWND_MESSAGE,nullptr,receiverClass.hInstance,nullptr);
 WindowEvents events;check(receiver&&events.start(receiver),"real window event hooks start for owned receiver");events.acknowledge(second);
 auto identitiesBefore=collector.statistics();
 check(fixture&&SendMessageW(fixture,WM_APP+90,0,0)==1,"owned primary AUMID changes and emits normal name event");pumpFor(120);
 int pendingRefreshes=refreshMessages;
 check(secondFixture&&SendMessageW(secondFixture,WM_APP+90,0,0)==1,"owned secondary AUMID changes during pending debounce");pumpFor(120);
 auto invalidated=events.takeIdentityInvalidations();
 check(std::find(invalidated.begin(),invalidated.end(),fixture)!=invalidated.end(),"normal event retains primary identity invalidation");
 check(std::find(invalidated.begin(),invalidated.end(),secondFixture)!=invalidated.end(),"debounced event burst retains secondary identity invalidation");
 check(pendingRefreshes==1&&refreshMessages==1,"event bursts schedule one refresh while gate is pending");
 check(events.takeIdentityInvalidations().empty(),"identity invalidation queue drains without replaying entries");
 for(auto hwnd:invalidated)collector.invalidateIdentity(hwnd);
 auto normal=enriched(collector);auto identitiesAfter=collector.statistics();if(identitiesAfter.pathQueries!=identitiesBefore.pathQueries||identitiesAfter.identityQueries<identitiesBefore.identityQueries+2)cacheDiagnostics("identity invalidation",identitiesBefore,identitiesAfter,second,normal);
 check(identityOf(normal,fixture)==L"aumid:aether.fixture.two","normal cached snapshot refreshes primary AUMID without reconciliation");
 check(identityOf(normal,secondFixture)==L"aumid:aether.fixture.two","normal cached snapshot refreshes burst AUMID without reconciliation");
 check(identitiesAfter.pathQueries==identitiesBefore.pathQueries,"identity invalidation keeps live process paths cached");
 check(identitiesAfter.identityQueries>=identitiesBefore.identityQueries+2,"identity invalidation queries affected window properties");
 events.acknowledge(normal);
 check(fixture&&SendMessageW(fixture,WM_APP+91,0,0)==1,"owned AUMID changes with synthetic destroy/show lifecycle events");pumpFor(120);
 auto lifecycle=events.takeIdentityInvalidations();
 check(std::find(lifecycle.begin(),lifecycle.end(),fixture)!=lifecycle.end(),"lifecycle events invalidate a cached HWND in the same process"); auto actualLifecycle=events.takeLifecycleChanges();check(std::find(actualLifecycle.begin(),actualLifecycle.end(),fixture)!=actualLifecycle.end(),"lifecycle-only queue retains owned destroy event separately from name changes");check(events.takeLifecycleChanges().empty(),"lifecycle-only queue drains once");
 for(auto hwnd:lifecycle)collector.invalidateIdentity(hwnd);
 auto replaced=enriched(collector);check(identityOf(replaced,fixture)==L"aumid:aether.fixture.three","same-process lifecycle invalidation refreshes ordinary snapshot identity");
 events.acknowledge(replaced); check(SendMessageW(fixture,WM_APP+93,0,0)==1,"owned fixture emits drag start");pumpFor(120);check(events.moving(),"window drag start pauses tiling through event adapter");
 check(SendMessageW(fixture,WM_APP+94,0,0)==1,"owned fixture emits drag end");pumpFor(120);auto moveEnds=events.takeUserMoveEnds();check(!events.moving()&&std::find(moveEnds.begin(),moveEnds.end(),fixture)!=moveEnds.end(),"drag end clears moving state and preserves manual-release HWND");check(events.takeUserMoveEnds().empty(),"user move-end queue drains once");
 check(fixture&&SendMessageW(fixture,WM_APP+90,0,0)==1,"owned AUMID returns to reconciliation fixture value");pumpFor(120);
 events.pause(true);check(events.takeIdentityInvalidations().empty(),"paused event source clears stale identity invalidations");events.pause(false);
 events.stop();DestroyWindow(receiver);
 auto refreshed=enriched(collector,true);auto item=std::find_if(refreshed.begin(),refreshed.end(),[&](const Window& w){return w.hwnd==fixture;});check(item!=refreshed.end()&&item->identity==L"aumid:aether.fixture.two","reconciliation invalidates changed Shell identity");
 if(fixture)PostMessageW(fixture,WM_CLOSE,0,0);bool stopped=WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0;check(stopped,"owned fixture exits cooperatively");auto closed=collector.snapshot();check(std::none_of(closed.begin(),closed.end(),[&](const Window& w){return w.hwnd==fixture;}),"closed HWND disappears from cached snapshots");
 std::cout<<checks-failures<<'/'<<checks<<" desktop policy checks passed\n";return failures?1:0;
}


