#include "app.hpp"
#include "platform/config.hpp"
#include "platform/provider_client.hpp"
#include <commctrl.h>
#include <tlhelp32.h>
#include <iostream>
#include <functional>
namespace {
bool waitFor(const std::function<bool()>& predicate,DWORD ms){
 const auto deadline=GetTickCount64()+ms;
 do{MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}if(predicate())return true;Sleep(25);}while(GetTickCount64()<deadline);return false;
}
HWND controller(DWORD pid){
 struct Search{DWORD pid;HWND window=nullptr;} search{pid};
 EnumWindows([](HWND window,LPARAM context)->BOOL{
  auto& search=*reinterpret_cast<Search*>(context);DWORD actual=0;GetWindowThreadProcessId(window,&actual);
  if(actual==search.pid&&aether::win::windowText(window)==L"Aether UI Controller"){search.window=window;return FALSE;}return TRUE;
 },reinterpret_cast<LPARAM>(&search));return search.window;
}
std::vector<aether::win::Handle> children(DWORD pid){
 aether::win::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
 std::vector<aether::win::Handle> result;PROCESSENTRY32W item{};item.dwSize=sizeof(item);
 if(snapshot&&Process32FirstW(snapshot.value,&item))do{
  if(item.th32ParentProcessID==pid){std::cout<<"Observed child "<<item.th32ProcessID<<" "<<aether::win::utf8(item.szExeFile)<<"\n";if(_wcsicmp(item.szExeFile,aether::win::executablePath().filename().c_str())==0){aether::win::Handle process(OpenProcess(SYNCHRONIZE,FALSE,item.th32ProcessID));if(process)result.push_back(std::move(process));}}
 }while(Process32NextW(snapshot.value,&item));return result;
}
bool taskbarsVisible(){
 bool result=true;
 EnumWindows([](HWND window,LPARAM context)->BOOL{wchar_t name[80]{};GetClassNameW(window,name,80);
  if((wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0)&&!IsWindowVisible(window))*reinterpret_cast<bool*>(context)=false;return TRUE;
 },reinterpret_cast<LPARAM>(&result));return result;
}
}
int wmain(int argc,wchar_t** argv){
 using namespace aether;using namespace aether::win;
 if(argc>1&&std::wstring_view(argv[1])==L"--provider-channel"){
  return runProviderHost(argc,argv,[](ProviderOperation operation,const ProviderBytes&,std::wstring_view mode)->ProviderWireReply{
   std::wstring name(mode);name+=operation==ProviderOperation::Catalog?L".catalog":operation==ProviderOperation::Icon?L".icon":operation==ProviderOperation::Status?L".status":L".other";
   Handle observed(OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str()));if(observed)SetEvent(observed.value);
   Sleep(INFINITE);return {};
  });
 }
 ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 if(argc==4&&std::wstring_view(argv[1])==L"--fixture-app"){
  setConfigDirectory(argv[2]);ProviderProcessOptions options;options.executable=executablePath();options.mode=argv[3];
  // Keep entered fixture operations blocked beyond all readiness/admission waits.
  // Product deadlines remain unchanged; normal quit must cancel these helpers.
  options.catalogTimeoutMs=60000;options.iconTimeoutMs=60000;options.metadataTimeoutMs=60000;
  App app(options);return app.run();
 }
 if(argc!=1||FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"))return 2;
 const auto prefix=L"Local\\Aether.BlockedApp."+std::to_wstring(GetCurrentProcessId());
 Handle catalogEvent(CreateEventW(nullptr,TRUE,FALSE,(prefix+L".catalog").c_str()));
 Handle iconEvent(CreateEventW(nullptr,TRUE,FALSE,(prefix+L".icon").c_str()));
 Handle statusEvent(CreateEventW(nullptr,TRUE,FALSE,(prefix+L".status").c_str()));
 const auto directory=executablePath().parent_path()/L"test-profiles"/(L"blocked-app-"+std::to_wstring(GetCurrentProcessId()));
 setConfigDirectory(directory);Config config;config.hideTaskbar=false;config.pins={{"Fixture",utf8(executablePath().native()),utf8(lower(executablePath().native()))}};saveConfig(config);
 const auto before=monitors();
 std::wstring command=L"\""+executablePath().native()+L"\" --fixture-app \""+directory.native()+L"\" \""+prefix+L"\"";
 STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};
 if(!CreateProcessW(executablePath().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created))return 3;
 Handle process(created.hProcess),thread(created.hThread);
 int checks=0,failures=0;auto check=[&](bool ok,const char* text){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
 HWND window=nullptr;
 check(waitFor([&]{window=controller(created.dwProcessId);return window&&reinterpret_cast<ULONG_PTR>(GetPropW(window,AppLifecycleProperty))==static_cast<ULONG_PTR>(AppLifecyclePhase::Ready);},15000),"actual App completes initialization before the shutdown deadline starts");
 waitFor([&]{return WaitForSingleObject(catalogEvent.value,0)==WAIT_OBJECT_0&&WaitForSingleObject(iconEvent.value,0)==WAIT_OBJECT_0&&WaitForSingleObject(statusEvent.value,0)==WAIT_OBJECT_0;},12000);
 check(WaitForSingleObject(catalogEvent.value,0)==WAIT_OBJECT_0,"actual App catalog helper is blocked inside provider operation");
 check(WaitForSingleObject(iconEvent.value,0)==WAIT_OBJECT_0,"actual App icon helper is blocked inside provider operation");
 check(WaitForSingleObject(statusEvent.value,0)==WAIT_OBJECT_0,"actual App stays responsive while its status helper is blocked inside provider operation");
 auto owned=children(created.dwProcessId);check(owned.size()>=3&&owned.size()<=4,"catalog/icon/status helpers and optional metadata helper are the only owned providers running");
 const auto begin=GetTickCount64();if(window)PostMessageW(window,WM_CLOSE,0,0);
 DWORD wait=WAIT_TIMEOUT;ULONG_PTR previousPhase=0;
 while(GetTickCount64()-begin<3000){
  const auto phase=window?reinterpret_cast<ULONG_PTR>(GetPropW(window,AppLifecycleProperty)):0;
  if(phase&&phase!=previousPhase){std::cout<<"Shutdown phase "<<phase<<" at "<<GetTickCount64()-begin<<" ms\n";previousPhase=phase;}
  wait=WaitForSingleObject(process.value,25);if(wait!=WAIT_TIMEOUT)break;
 }
 const auto elapsed=GetTickCount64()-begin;DWORD code=STILL_ACTIVE;GetExitCodeProcess(process.value,&code);
 std::cout<<"Shutdown wait="<<wait<<" exit="<<code<<" elapsed="<<elapsed<<" ms lastPhase="<<previousPhase<<"\n";
 check(wait==WAIT_OBJECT_0&&code==0&&elapsed<3000,"actual App quits cooperatively in under three seconds with both providers blocked");
 bool retired=!owned.empty();for(const auto& helper:owned)retired=retired&&WaitForSingleObject(helper.value,1000)==WAIT_OBJECT_0;
 check(retired,"all observed blocked provider processes are gone after normal App quit");
 check(waitFor([&]{return monitors()==before;},4000),"original monitor work areas restore after blocked-provider quit");
 check(taskbarsVisible(),"native Windows taskbars remain visible");
 std::cout<<checks-failures<<'/'<<checks<<" blocked App shutdown checks passed\n";return failures?1:0;
}



