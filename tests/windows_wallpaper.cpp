#include "platform/wallpaper.hpp"
#include "platform/wallpaper_codec.hpp"
#include "platform/wallpaper_provider.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
using namespace aether;using namespace aether::win;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;}
// This backend never invokes any Windows wallpaper mutator.
struct OwnedBackend final:WallpaperBackend {
 WallpaperSnapshot state{{{{L"owned-left",{-1920,0,0,1080}},L"C:\\fixture-left.jpg"},{{L"owned-main",{0,0,2560,1440}},L"C:\\fixture-main.jpg"}},WallpaperFit::Center,0x123456,false};
 bool failImage=false;
 std::optional<WallpaperSnapshot> query() override{return state;}
 bool position(WallpaperFit value) override{state.fit=value;return true;}
 bool background(std::uint32_t value) override{state.color=value;return true;}
 bool image(const std::wstring& id,const std::wstring& path) override{if(failImage)return false;for(auto& m:state.monitors)if(m.display.device==id){m.path=path;return true;}return false;}
};
std::vector<Monitor> ownedDisplays(){Monitor left;left.name=L"AetherOwnedLeft";left.bounds={-1920,0,0,1080};left.work=left.bounds;Monitor main;main.name=L"AetherOwnedMain";main.bounds={0,0,2560,1440};main.work=main.bounds;return {left,main};}
ProviderProcessOptions fixtureOptions(std::wstring mode=L"owned-wallpaper"){ProviderProcessOptions options;options.executable=executablePath();options.mode=std::move(mode);options.pinTimeoutMs=1000;options.launchTimeoutMs=1000;options.idleMs=200;return options;}
bool waitFor(const std::function<bool()>& predicate,DWORD deadline=4000){const auto end=GetTickCount64()+deadline;do{MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(predicate())return true;Sleep(10);}while(GetTickCount64()<end);return false;}
std::optional<WallpaperCompletion> completion(WallpaperService& service,std::uint64_t id,DWORD deadline=4000){std::optional<WallpaperCompletion> value;waitFor([&]{for(auto& item:service.takeCompletions())if(item.id==id)value=std::move(item);return value.has_value();},deadline);return value;}
LRESULT CALLBACK receiverProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WallpaperMessage){auto* count=reinterpret_cast<int*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(count)++*count;return 0;}return DefWindowProcW(h,m,w,l);}
void wireTests(){
 OwnedBackend backend;WallpaperRequest request;request.kind=WallpaperKind::Apply;request.displays={backend.state.monitors[0].display,backend.state.monitors[1].display};request.image=L"C:\\chosen.png";request.selectedDevice=L"owned-left";auto bytes=encodeWallpaperRequest(request);auto decoded=decodeWallpaperRequest(bytes);
 check(decoded&&decoded->image==L"C:\\chosen.png"&&decoded->displays.front().bounds.left==-1920&&decoded->selectedDevice==L"owned-left","bounded wallpaper wire preserves path device and negative physical bounds");
 auto malformed=bytes;malformed.push_back(0);check(!decodeWallpaperRequest(malformed),"wallpaper request rejects trailing bytes");
 malformed=bytes;malformed.pop_back();check(!decodeWallpaperRequest(malformed),"wallpaper request rejects truncation");
 malformed=bytes;malformed[0]=0xff;check(!decodeWallpaperRequest(malformed),"wallpaper request rejects unknown version");
 request.displays.resize(17);check(encodeWallpaperRequest(request).empty(),"wallpaper wire caps monitor count before provider admission");
 WallpaperResult reply;reply.kind=WallpaperKind::Apply;reply.status=WallpaperStatus::Partial;reply.before=backend.state;reply.after=backend.state;reply.message=L"Observed partial outcome";auto response=encodeWallpaperResult(reply);auto result=decodeWallpaperResult(response);
 check(result&&result->status==WallpaperStatus::Partial&&result->before&&result->before->color==0x123456&&result->message==L"Observed partial outcome","partial reply retains prior state for user review");
 response.push_back(0);check(!decodeWallpaperResult(response),"wallpaper reply rejects trailing bytes");
}
void ownedServiceTests(){
 WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"Aether.OwnedWallpaperReceiver";cls.lpfnWndProc=receiverProc;RegisterClassW(&cls);auto receiver=CreateWindowExW(0,cls.lpszClassName,L"Owned wallpaper receiver",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);int notifications=0;SetWindowLongPtrW(receiver,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&notifications));
 const auto displays=ownedDisplays();WallpaperService service(fixtureOptions());service.start(receiver);auto queried=completion(service,service.requestQuery(displays,11));
 // Completion publication and PostMessage are asynchronous to this pump. The
 // result can be drained after the pump but before the queued message dispatch.
 const bool notified=waitFor([&]{return notifications>0;});
 check(queried&&queried->scope==11&&queried->result.status==WallpaperStatus::Ok&&notified,"service publishes scoped passive query through controller notification");
 auto inspected=completion(service,service.requestQuery(displays,999,true));check(inspected&&inspected->scope==999&&inspected->result.status==WallpaperStatus::Ok,"palette inspection uses an independent read-only scope");
 auto stale=displays;stale[0].bounds.right=1;check(service.requestApply(stale,11,L"C:\\chosen.png",WallpaperFit::Fill,displays[0].name)==0,"service rejects stale physical display map before helper mutation");
 auto applied=completion(service,service.requestApply(displays,11,L"C:\\chosen.png",WallpaperFit::Fill,displays[0].name));
 check(applied&&applied->result.status==WallpaperStatus::Ok&&service.canUndo()&&applied->result.after->monitors[0].path==L"C:\\chosen.png"&&applied->result.after->monitors[1].path==L"C:\\fixture-main.jpg","owned fake Apply updates only selected monitor and arms one successful Undo");
 auto undone=completion(service,service.requestUndo(displays,11));check(undone&&undone->result.status==WallpaperStatus::Ok&&!service.canUndo()&&undone->result.after->fit==WallpaperFit::Center,"owned fake Undo restores prior global position and consumes successful snapshot");
 auto applyAgain=completion(service,service.requestApply(displays,11,L"C:\\another.png",WallpaperFit::Fit,{}));check(applyAgain&&applyAgain->result.status==WallpaperStatus::Ok&&service.canUndo(),"All Apply creates fresh service-owned undo snapshot");
 // Idle provider retirement must not erase the service-owned snapshot; a new fixture helper has old state.
 Sleep(400);auto idleUndo=completion(service,service.requestUndo(displays,11));check(idleUndo&&idleUndo->result.status==WallpaperStatus::Stale&&service.canUndo(),"helper lifetime loss cannot make stale Undo overwrite current state and preserves last successful snapshot");
 service.stop();check(service.requestQuery(displays,12)==0&&service.requestUndo(displays,11)==0,"stopped service rejects new requests");
 if(receiver)DestroyWindow(receiver);
}
void cancellationTests(){
 const auto eventName=L"Local\\Aether.Wallpaper.Stall."+std::to_wstring(GetCurrentProcessId());Handle entered(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));auto options=fixtureOptions(L"stall-wallpaper:"+eventName);options.pinTimeoutMs=10000;WallpaperService stalled(options);stalled.start(nullptr);auto started=GetTickCount64();auto id=stalled.requestQuery(ownedDisplays(),1);check(id&&GetTickCount64()-started<200&&WaitForSingleObject(entered.value,2500)==WAIT_OBJECT_0,"wallpaper request is nonblocking and owned helper actually enters stall");started=GetTickCount64();stalled.stop();check(GetTickCount64()-started<1800,"stop cancels stuck wallpaper COM helper with bounded worker retirement");
 options=fixtureOptions(L"timeout-apply:"+eventName);options.launchTimeoutMs=150;WallpaperService uncertain(options);uncertain.start(nullptr);auto query=completion(uncertain,uncertain.requestQuery(ownedDisplays(),2));auto result=query?completion(uncertain,uncertain.requestApply(ownedDisplays(),2,L"C:\\uncertain.png",WallpaperFit::Fill,{})):std::nullopt;
 check(result&&result->outcome==ProviderOutcome::Timeout&&result->result.status==WallpaperStatus::Uncertain,"timed-out Apply reports unknown outcome");
 Sleep(250);check(uncertain.takeCompletions().empty()&&!uncertain.canUndo(),"timed-out mutation produces exactly one completion without auto retry or false Undo");uncertain.stop();
}
void preservedUndoTests(){
 auto options=fixtureOptions();WallpaperService service(options);service.start(nullptr);auto query=completion(service,service.requestQuery(ownedDisplays(),21));auto initial=query?completion(service,service.requestApply(ownedDisplays(),21,L"C:\\successful.png",WallpaperFit::Fill,{})):std::nullopt;
 auto partial=initial?completion(service,service.requestApply(ownedDisplays(),21,L"C:\\owned-fail.png",WallpaperFit::Fit,{})):std::nullopt;
 check(partial&&partial->result.status==WallpaperStatus::Partial&&service.canUndo(),"partial fake Apply preserves last successful service undo snapshot");
 auto undo=partial?completion(service,service.requestUndo(ownedDisplays(),21)):std::nullopt;check(undo&&undo->result.status==WallpaperStatus::Stale&&service.canUndo(),"Undo after a partial attempt rejects changed fit rather than replacing successful snapshot");service.stop();
 options=fixtureOptions(L"malformed-apply");WallpaperService malformed(options);malformed.start(nullptr);query=completion(malformed,malformed.requestQuery(ownedDisplays(),22));auto wrong=query?completion(malformed,malformed.requestApply(ownedDisplays(),22,L"C:\\new.png",WallpaperFit::Fill,{})):std::nullopt;
 check(wrong&&wrong->outcome==ProviderOutcome::Malformed&&wrong->result.status==WallpaperStatus::Uncertain&&!malformed.canUndo(),"helper success with mismatched observed state cannot arm false Undo");malformed.stop();
}
void readOnlyRealQuery(){
 ProviderProcessOptions options;options.executable=executablePath().parent_path()/L"AetherWallpaperProvider.exe";ProviderClient provider(options);auto reply=provider.call(static_cast<ProviderOperation>(8),encodeWallpaperRequest({}),nullptr,4000);auto result=reply.outcome==ProviderOutcome::Ok?decodeWallpaperResult(reply.bytes):std::nullopt;
 check(reply.outcome==ProviderOutcome::Ok&&result&&(result->status==WallpaperStatus::Ok||result->status==WallpaperStatus::Unavailable),"real Windows wallpaper provider permits passive Query only in native validation");if(result)std::cout<<"READONLY real wallpaper status "<<static_cast<unsigned>(result->status)<<" monitors "<<(result->after?result->after->monitors.size():0)<<'\n';provider.close();
}
}
int wmain(int argc,wchar_t** argv){
 if(argc==7&&std::wstring_view(argv[1])==L"--provider-channel")return runProviderHost(argc,argv,[](ProviderOperation operation,const ProviderBytes& bytes,std::wstring_view mode)->ProviderWireReply{
  const auto raw=static_cast<std::uint32_t>(operation);if(raw<8||raw>10)return {1,{}};auto request=decodeWallpaperRequest(bytes);if(!request||static_cast<std::uint32_t>(request->kind)+8!=raw)return {1,{}};
  if(mode.rfind(L"stall-wallpaper:",0)==0||(mode.rfind(L"timeout-apply:",0)==0&&request->kind==WallpaperKind::Apply)){auto name=std::wstring(mode.substr(mode.find(L':')+1));Handle entered(OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str()));if(entered)SetEvent(entered.value);Sleep(INFINITE);}
  if(mode!=L"owned-wallpaper"&&mode!=L"malformed-apply"&&mode.rfind(L"timeout-apply:",0)!=0)return {1,{}};
  static OwnedBackend backend;
  if(mode==L"malformed-apply"&&request->kind==WallpaperKind::Apply){WallpaperResult fake;fake.kind=WallpaperKind::Apply;fake.status=WallpaperStatus::Ok;fake.before=backend.state;fake.after=backend.state;return {0,encodeWallpaperResult(fake)};}
  backend.failImage=request->kind==WallpaperKind::Apply&&request->image==L"C:\\owned-fail.png";auto result=executeWallpaper(backend,*request);backend.failImage=false;return {0,encodeWallpaperResult(result)};
 });
 if(argc!=1)return 2;try{wireTests();ownedServiceTests();cancellationTests();preservedUndoTests();readOnlyRealQuery();}catch(const std::exception& error){check(false,error.what());}
 std::cout<<checks-failures<<'/'<<checks<<" native wallpaper checks passed\n";return failures?1:0;
}
