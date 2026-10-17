#include "app.hpp"
#include "ui/extensions.hpp"
#include "platform/provider_codec.hpp"
#include <iostream>
#include <oleacc.h>
namespace {
using namespace aether;
constexpr const wchar_t* FixtureTarget=L"C:\\AetherOwnedNeverLaunch-ExtensionFixture.exe";
struct Fixture {
 App* app=nullptr;win::Handle launched;win::ComPtr<IAccessible> action;HWND widget=nullptr;
 UINT_PTR timer=0;int phase=0,checks=0,failures=0;ULONGLONG deadline=0;
 void check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<text<<'\n';}}
 void finish(){if(timer){KillTimer(nullptr,timer);timer=0;}action.Reset();if(app)app->quit();}
};
Fixture* fixture=nullptr;
HWND actionWindow(){HWND found=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND hwnd,LPARAM data)->BOOL{if(win::windowText(hwnd)==L"Aether extension: Visibility fixture / Launch owned fixture"){*reinterpret_cast<HWND*>(data)=hwnd;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&found));return found;}
void CALLBACK timerCallback(HWND,UINT,UINT_PTR,DWORD){
 if(!fixture||!fixture->app||!fixture->app->controller())return;auto& test=*fixture;auto now=GetTickCount64();
 try{
  if(test.phase==0){
   auto& runtime=test.app->extensions();runtime.pause(true);std::string error;test.check(runtime.setEnabled("preset-000001",true,&error),"Owned paused quick-action package must enable for the visibility regression.");
   test.widget=actionWindow();test.check(test.widget&&IsWindowVisible(test.widget)==FALSE,"The fixture quick action must be hidden before retaining its accessible action.");
   if(!test.widget){test.finish();return;}auto hr=AccessibleObjectFromWindow(test.widget,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(test.action.GetAddressOf()));test.check(SUCCEEDED(hr)&&test.action,"Hidden quick action must expose the retained MSAA client for the regression.");
   if(!test.action){test.finish();return;}VARIANT child{};child.vt=VT_I4;child.lVal=4;BSTR label=nullptr;auto named=test.action->get_accName(child,&label);test.check(SUCCEEDED(named)&&label&&std::wstring(label)==L"Launch owned fixture","Retained child must be the exact configured-pin quick action.");SysFreeString(label);
   // Positive calibration exercises the running App's real launch queue and only the fake provider.
   test.app->launchTarget(test.app->controller(),FixtureTarget);test.phase=1;test.deadline=now+6000;return;
  }
  if(test.phase==1){
   if(WaitForSingleObject(test.launched.value,0)!=WAIT_OBJECT_0){if(now>=test.deadline){test.check(false,"Owned fake launch provider failed positive calibration.");test.finish();}return;}
   test.check(true,"Owned fake launch provider observes a configured launch without starting any app.");ResetEvent(test.launched.value);test.app->extensions().pause(true);ShowWindow(test.widget,SW_HIDE);
   test.check(IsWindowVisible(test.widget)==FALSE,"The exact action window must remain hidden at invocation.");
   VARIANT child{};child.vt=VT_I4;child.lVal=4;test.action->accDoDefaultAction(child);test.phase=2;test.deadline=now+1800;return;
  }
  if(test.phase==2&&now>=test.deadline){test.check(WaitForSingleObject(test.launched.value,0)==WAIT_TIMEOUT,"Retained hidden-widget MSAA action must never enter the App launch queue.");test.finish();}
 }catch(const std::exception& e){test.check(false,e.what());test.finish();}
}
}
int wmain(int argc,wchar_t** argv){
 using namespace aether;using namespace aether::win;
 if(argc>1&&std::wstring_view(argv[1])==L"--provider-channel")return runProviderHost(argc,argv,[](ProviderOperation operation,const ProviderBytes& payload,std::wstring_view mode)->ProviderWireReply{
  // This handler has no launch implementation: no ShellExecute, CreateProcess of targets or real-app effects.
  switch(operation){
  case ProviderOperation::Launch:{auto request=decodeLaunch(payload);if(!request||request->target!=FixtureTarget)return {1,{}};Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,std::wstring(mode).c_str()));if(!event)return {1,{}};SetEvent(event.value);return {};}
  case ProviderOperation::Catalog:return {0,encodeApplications({})};
  case ProviderOperation::Icon:case ProviderOperation::Image:return {0,encodeIcon(nullptr)};
  case ProviderOperation::WindowMetadata:return {0,encodeWindowMetadata({})};
  default:return {1,{}};
  }
 });
 if(argc!=1||FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Visibility fixture requires its own Aether controller.\n";return 2;}
 ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 auto directory=executablePath().parent_path()/L"test-profiles"/(L"extension-visibility-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory/L"extensions");setConfigDirectory(directory);
 Config config;config.defaults.topBar=false;config.defaults.dock=false;config.defaults.reserveDock=false;config.hideTaskbar=false;config.tilingDefaults.enabled=false;config.pins={{"Owned fake launch",utf8(FixtureTarget),"owned:extension-visibility"}};saveConfig(config);
 writeAtomic(directory/L"extensions"/L"preset-000001.aetherplugin","[AetherPlugin]\nversion=1\n[plugin]\nid=visibility-fixture\nname=Visibility fixture\n[widget:launch]\ntype=quick-action\nlabel=Launch owned fixture\naction=pin\npin=owned:extension-visibility\n[AetherInstallation]\nversion=1\nenabled=0\norder=0\n");
 auto eventName=L"Local\\Aether.ExtensionVisibility."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());Fixture test;test.launched.reset(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));if(!test.launched)return 2;
 ProviderProcessOptions options;options.executable=executablePath();options.mode=eventName;options.launchTimeoutMs=3000;
 auto before=monitors();int exitCode=2;
 {App app(options,options);test.app=&app;fixture=&test;test.timer=SetTimer(nullptr,0,25,timerCallback);if(!test.timer)return 2;exitCode=app.run();fixture=nullptr;test.app=nullptr;}
 if(test.timer)KillTimer(nullptr,test.timer);test.check(exitCode==0,"Owned controller and fake providers must exit normally.");test.check(monitors()==before,"Bars-disabled visibility fixture must preserve monitor work areas.");
 std::cout<<"Extension visibility: "<<(test.checks-test.failures)<<"/"<<test.checks<<" checks passed.\n";return test.failures?2:0;
}