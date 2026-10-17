#include "app.hpp"
#include "platform/config.hpp"
#include "platform/provider_codec.hpp"
#include <commctrl.h>
#include <tlhelp32.h>
#include <functional>
#include <iostream>
using namespace aether;using namespace aether::win;
namespace {
App* fixtureApp=nullptr;
void CALLBACK launchTimer(HWND,UINT,UINT_PTR timer,DWORD){
 if(!fixtureApp||!fixtureApp->controller())return;
  KillTimer(nullptr,timer);fixtureApp->openActivities();
 auto launcher=FindWindowW(nullptr,L"Aether Activities");if(!launcher)return;
 auto edit=GetDlgItem(launcher,10);SetWindowTextW(edit,L"Aether blockedfixture");
 RECT original{};GetWindowRect(launcher,&original);auto dpi=dpiFor(launcher);
 auto point=[&](int x,int y){return MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));};
 SetWindowPos(launcher,nullptr,0,0,original.right-original.left,MulDiv(200,dpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
 SendMessageW(launcher,WM_LBUTTONUP,0,point(100,175));
 SetWindowPos(launcher,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE);
 SendMessageW(launcher,WM_MOUSEMOVE,0,point(100,160));
 SendMessageW(launcher,WM_LBUTTONUP,0,point(100,215));
}
bool waitFor(const std::function<bool()>& predicate,DWORD ms){
 auto deadline=GetTickCount64()+ms;
 do{if(predicate())return true;Sleep(20);}while(GetTickCount64()<deadline);return false;
}
HWND controller(DWORD pid){
 struct Search{DWORD pid;HWND hwnd=nullptr;} search{pid};
 EnumWindows([](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid==s.pid&&windowText(h)==L"Aether UI Controller"){s.hwnd=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.hwnd;
}
std::vector<Handle> helpers(DWORD parent){
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W item{sizeof(item)};std::vector<Handle> result;
 if(snapshot&&Process32FirstW(snapshot.value,&item))do{if(item.th32ParentProcessID==parent&&_wcsicmp(item.szExeFile,executablePath().filename().c_str())==0){Handle h(OpenProcess(SYNCHRONIZE,FALSE,item.th32ProcessID));if(h)result.push_back(std::move(h));}}while(Process32NextW(snapshot.value,&item));return result;
}
}
int wmain(int argc,wchar_t** argv){
 if(argc>1&&std::wstring_view(argv[1])==L"--provider-channel")
  return runProviderHost(argc,argv,[](ProviderOperation op,const ProviderBytes& payload,std::wstring_view mode)->ProviderWireReply{
   std::wstring name(mode);name+=L"."+std::to_wstring(static_cast<unsigned>(op));Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str()));if(event)SetEvent(event.value);
   if(op==ProviderOperation::Launch){auto launch=decodeLaunch(payload);if(launch&&launch->target==L"C:\\AetherOwnedBlockedLaunch-2.exe"){Handle correct(OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(mode)+L".clicked-correct").c_str()));if(correct)SetEvent(correct.value);}}
   Sleep(INFINITE);return {};
  });
 ComApartment apartment;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
 if(argc==4&&std::wstring_view(argv[1])==L"--fixture-app"){
  setConfigDirectory(argv[2]);ProviderProcessOptions options;options.executable=executablePath();options.mode=argv[3];options.metadataTimeoutMs=10000;options.pinTimeoutMs=10000;options.launchTimeoutMs=10000;
  App app(options);fixtureApp=&app;SetTimer(nullptr,0,100,launchTimer);auto code=app.run();fixtureApp=nullptr;return code;
 }
 if(argc!=1||FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"))return 2;
 auto prefix=L"Local\\Aether.ShellApp."+std::to_wstring(GetCurrentProcessId());
 Handle correctClick(CreateEventW(nullptr,TRUE,FALSE,(prefix+L".clicked-correct").c_str()));
 std::vector<Handle> signals;for(unsigned operation:{1u,2u,4u,6u})signals.emplace_back(CreateEventW(nullptr,TRUE,FALSE,(prefix+L"."+std::to_wstring(operation)).c_str()));
 auto directory=executablePath().parent_path()/L"test-profiles"/(L"blocked-shell-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 setConfigDirectory(directory);Config config;config.hideTaskbar=false;config.pins={{"Owned unresolved shortcut",utf8((directory/L"owned.lnk").native()),"path:fixture"},{"Aether blockedfixture 01","C:\\AetherOwnedBlockedLaunch-1.exe","fixture:first"},{"Aether blockedfixture 02","C:\\AetherOwnedBlockedLaunch-2.exe","fixture:second"}};saveConfig(config);
 auto before=monitors();std::wstring command=L"\""+executablePath().native()+L"\" --fixture-app \""+directory.native()+L"\" \""+prefix+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};
 if(!CreateProcessW(executablePath().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created))return 3;
 Handle process(created.hProcess),thread(created.hThread);bool pass=true;auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';pass=pass&&ok;};HWND h=nullptr;
 check(waitFor([&]{h=controller(created.dwProcessId);return h!=nullptr;},10000),"App starts before blocked shortcut resolution");
 check(waitFor([&]{for(const auto& signal:signals)if(WaitForSingleObject(signal.value,0)!=WAIT_OBJECT_0)return false;return true;},8000),"catalog, icon, pin and launch helpers stall concurrently without starving launch");
 check(WaitForSingleObject(correctClick.value,0)==WAIT_OBJECT_0,"running App ignores short footer and launches clicked row rather than stale hover");
 DWORD_PTR reply=0;auto responsive=SendMessageTimeoutW(h,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,500,&reply)!=0;
 check(responsive,"actual controller responds while Shell operations are blocked");
 auto owned=helpers(created.dwProcessId);check(owned.size()>=4,"blocked providers are observed and owned");
 auto begin=GetTickCount64();if(h)PostMessageW(h,WM_CLOSE,0,0);bool exited=WaitForSingleObject(process.value,3000)==WAIT_OBJECT_0;DWORD code=1;if(exited)GetExitCodeProcess(process.value,&code);
 check(exited&&code==0&&GetTickCount64()-begin<3000,"App shuts down cooperatively under three seconds");
 bool retired=!owned.empty();for(const auto& helper:owned)retired=retired&&WaitForSingleObject(helper.value,1000)==WAIT_OBJECT_0;check(retired,"blocked providers retire after App shutdown");
  bool restored=waitFor([&]{return monitors()==before;},4000);
 if(!restored){auto after=monitors();for(std::size_t i=0;i<before.size();++i)std::cout<<"Before "<<utf8(before[i].name)<<" "<<before[i].work.left<<","<<before[i].work.top<<","<<before[i].work.right<<","<<before[i].work.bottom<<" dpi"<<before[i].dpi<<"\n";for(const auto& m:after)std::cout<<"After "<<utf8(m.name)<<" "<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<" dpi"<<m.dpi<<"\n";}
 check(restored,"work areas restored");
 bool visible=true;EnumWindows([](HWND window,LPARAM p)->BOOL{wchar_t name[80]{};GetClassNameW(window,name,80);if((wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0)&&!IsWindowVisible(window))*reinterpret_cast<bool*>(p)=false;return TRUE;},reinterpret_cast<LPARAM>(&visible));
 check(visible,"Windows taskbars remain visible");return pass?0:1;
}