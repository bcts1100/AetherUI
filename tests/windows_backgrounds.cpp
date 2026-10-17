#include "app.hpp"
#include "ui/backgrounds.hpp"
#include "platform/config.hpp"
#include "platform/wallpaper_codec.hpp"
#include <oleacc.h>
#include <functional>
#include <iostream>
using namespace aether;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
VARIANT child(long id){VARIANT value{};value.vt=VT_I4;value.lVal=id;return value;}
std::wstring name(IAccessible* a,long id){BSTR text{};auto hr=a->get_accName(child(id),&text);std::wstring result=text?text:L"";SysFreeString(text);require(SUCCEEDED(hr),"Accessible name unavailable");return result;}
win::ComPtr<IAccessible> accessible(HWND h){win::ComPtr<IAccessible> a;require(SUCCEEDED(AccessibleObjectFromWindow(h,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(a.GetAddressOf()))),"Backgrounds MSAA object missing");return a;}
long find(IAccessible* a,const std::wstring& text){long count{};a->get_accChildCount(&count);for(long i=1;i<=count;++i)if(name(a,i)==text)return i;return -1;}
bool waitFor(const std::function<bool()>& predicate,DWORD deadline=4000){const auto end=GetTickCount64()+deadline;do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(predicate())return true;Sleep(10);}while(GetTickCount64()<end);return false;}
// Apply and Undo in this test executable can only reach this owned in-memory backend.
struct OwnedUiBackend final:WallpaperBackend {
 WallpaperSnapshot state;
 OwnedUiBackend(){auto monitors=win::monitors();for(std::size_t i=0;i<monitors.size();++i)state.monitors.push_back({{L"owned-ui-device-"+std::to_wstring(i),monitors[i].bounds},L"C:\\owned-prior-"+std::to_wstring(i)+L".jpg"});}
 std::optional<WallpaperSnapshot> query() override{return validWallpaperSnapshot(state)?std::optional<WallpaperSnapshot>(state):std::nullopt;}
 bool position(WallpaperFit value) override{state.fit=value;return true;}
 bool background(std::uint32_t value) override{state.color=value;return true;}
 bool image(const std::wstring& id,const std::wstring& path) override{for(auto& monitor:state.monitors)if(monitor.display.device==id){monitor.path=path;return true;}return false;}
};
}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc==7&&std::wstring_view(argv[1])==L"--provider-channel")return win::runProviderHost(argc,argv,[](win::ProviderOperation operation,const win::ProviderBytes& bytes,std::wstring_view mode)->win::ProviderWireReply{
  const auto code=static_cast<std::uint32_t>(operation);if(code<8||code>10||mode.rfind(L"owned-background-ui:",0)!=0)return {1,{}};
  auto request=win::decodeWallpaperRequest(bytes);if(!request||static_cast<std::uint32_t>(request->kind)+8!=code)return {1,{}};
  if(request->kind!=WallpaperKind::Query){auto eventName=std::wstring(mode.substr(mode.find(L':')+1));win::Handle marker(OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName.c_str()));if(marker)SetEvent(marker.value);}
  static OwnedUiBackend backend;return {0,win::encodeWallpaperResult(executeWallpaper(backend,*request))};
 });
 if(argc!=1)return 2;std::filesystem::path directory;
 try{
 win::ComApartment apartment;
 directory=std::filesystem::temp_directory_path()/(L"AetherBackgroundUi-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory);win::setConfigDirectory(directory);
 const auto eventName=L"Local\\Aether.HiddenBackgroundMutation."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());win::Handle marker(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));require(static_cast<bool>(marker),"Owned mutation marker unavailable");
 win::ProviderProcessOptions fake;fake.executable=win::executablePath();fake.mode=L"owned-background-ui:"+eventName;fake.pinTimeoutMs=2000;fake.launchTimeoutMs=2000;fake.idleMs=10000;
 App app({},fake);app.wallpaper().start(nullptr);ui::Graphics graphics;auto displays=win::monitors();require(!displays.empty(),"No display available");win::ComPtr<IAccessible> retained;
 {
 ui::Backgrounds backgrounds(graphics,app);backgrounds.open(displays.front());std::uint64_t scope=0;
 require(waitFor([&]{bool received=false;for(auto& completion:app.wallpaper().takeCompletions()){scope=completion.scope;require(completion.kind==WallpaperKind::Query&&completion.result.status==WallpaperStatus::Ok,"Owned UI passive query failed");backgrounds.update(completion);received=true;}return received;}),"Owned UI query did not complete");
 UpdateWindow(backgrounds.hwnd());auto a=accessible(backgrounds.hwnd());
 require(find(a.Get(),L"Choose background image")>0&&find(a.Get(),L"Apply to selected display")>0&&find(a.Get(),L"Apply to all displays")>0&&find(a.Get(),L"Undo last background change")>0,"Background actions lack descriptive native names");
 auto input=GetDlgItem(backgrounds.hwnd(),301);require(input!=nullptr,"Native image path EDIT is missing");auto edit=accessible(input);require(name(edit.Get(),CHILDID_SELF)==L"Background image path","Native image path EDIT lacks accessible name");
 auto path=find(a.Get(),L"Background image path");win::ComPtr<IDispatch> native;require(path>0&&SUCCEEDED(a->get_accChild(child(path),&native))&&native,"Backgrounds root does not delegate EDIT accessibility");
 SetWindowTextW(input,L"C:\\passive-preview.png");SendMessageW(input,EM_SETSEL,4,4);SendMessageW(input,WM_KEYDOWN,VK_LEFT,0);DWORD start{},end{};SendMessageW(input,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));require(start==3&&end==3,"Path EDIT arrow navigation lost native caret behavior");
 require(!app.wallpaper().canUndo()&&WaitForSingleObject(marker.value,0)==WAIT_TIMEOUT,"Typing a selection implicitly changed wallpaper");
 SendMessageW(input,WM_KEYDOWN,VK_TAB,0);UpdateWindow(backgrounds.hwnd());a=accessible(backgrounds.hwnd());VARIANT focus{};a->get_accFocus(&focus);require(focus.vt==VT_I4&&name(a.Get(),focus.lVal)==L"Choose background image","Path Tab does not reach Choose action");
 auto selected=find(a.Get(),L"Apply to selected display");VARIANT state{};a->get_accState(child(selected),&state);require(state.vt==VT_I4&&!(state.lVal&STATE_SYSTEM_UNAVAILABLE),"Apply did not become enabled after a validated owned query");
 // Arm Undo using an explicit fake-only service request; no Windows wallpaper API is present in this fixture helper.
 const auto applyId=app.wallpaper().requestApply(displays,scope,L"C:\\owned-applied.png",WallpaperFit::Center,{});require(applyId!=0,"Owned baseline Apply was not admitted");
 require(waitFor([&]{for(auto& completion:app.wallpaper().takeCompletions())if(completion.id==applyId){require(completion.result.status==WallpaperStatus::Ok,"Owned baseline Apply failed");return true;}return false;}),"Owned baseline Apply did not complete");
 require(app.wallpaper().canUndo()&&WaitForSingleObject(marker.value,0)==WAIT_OBJECT_0,"Owned baseline did not arm a real service Undo snapshot");ResetEvent(marker.value);
 UpdateWindow(backgrounds.hwnd());a=accessible(backgrounds.hwnd());const auto undo=find(a.Get(),L"Undo last background change");state={};a->get_accState(child(undo),&state);require(state.vt==VT_I4&&!(state.lVal&STATE_SYSTEM_UNAVAILABLE),"Owned Undo was not exposed as enabled");
 retained=a;backgrounds.hide();
 // Retained MSAA actions carry the previously enabled node. Hidden invocation must still be rejected by the live UI.
 retained->accDoDefaultAction(child(undo));retained->accDoDefaultAction(child(selected));
 bool unexpected=false;waitFor([&]{if(!app.wallpaper().takeCompletions().empty())unexpected=true;return unexpected||WaitForSingleObject(marker.value,0)==WAIT_OBJECT_0;},700);
 require(!unexpected&&WaitForSingleObject(marker.value,0)==WAIT_TIMEOUT,"Hidden retained MSAA Apply/Undo admitted an owned mutation");
 backgrounds.open(displays.front());backgrounds.refreshTopology();SendMessageW(backgrounds.hwnd(),WM_KEYDOWN,VK_ESCAPE,0);require(!backgrounds.visible(),"Escape does not hide Backgrounds");
 }
 long count{};require(FAILED(retained->get_accChildCount(&count)),"Retained Backgrounds accessibility provider outlives detached HWND");
 app.wallpaper().stop();win::setConfigDirectory({});std::error_code error;std::filesystem::remove_all(directory,error);std::cout<<"PASS passive Backgrounds keyboard EDIT delegation MSAA hidden mutation rejection and lifetime\n";return 0;
 }catch(const std::exception& error){win::setConfigDirectory({});if(!directory.empty()){std::error_code ignored;std::filesystem::remove_all(directory,ignored);}std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
