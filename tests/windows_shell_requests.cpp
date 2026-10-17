#include "platform/shell_requests.hpp"
#include "platform/provider_codec.hpp"
#include "platform/provider_sources.hpp"
#include "platform/desktop.hpp"
#include <propkey.h>
#include <propvarutil.h>
#include <fstream>
#include <iostream>
#include <functional>
#include <sstream>
using namespace aether;using namespace aether::win;
namespace {
int checks=0,failures=0;
void check(bool ok,const char* text){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;}
bool waitFor(const std::function<bool()>& predicate,DWORD deadline=4000){const auto end=GetTickCount64()+deadline;do{MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(predicate())return true;Sleep(20);}while(GetTickCount64()<end);return false;}
// Functional native checks use the production budgets; stall cases override them explicitly.
ProviderProcessOptions selfOptions(){ProviderProcessOptions options;options.executable=executablePath();return options;}
// Trace only the exact temporary shortcut fixtures authored by this executable.
std::optional<std::filesystem::path> ownedLaunchTrace(std::wstring_view target){
 try{
  std::filesystem::path shortcut(target);auto directory=shortcut.parent_path();auto name=directory.filename().wstring();
  if(shortcut.filename()!=L"Tea launch.lnk"||!shortcut.is_absolute()||name.size()>128||name.rfind(L"AetherShell-",0)!=0)return {};
  std::error_code pathError;const auto tempDirectory=std::filesystem::temp_directory_path(pathError);
  if(pathError||!std::filesystem::equivalent(directory.parent_path(),tempDirectory,pathError)||pathError)return {};
  auto numbers=std::wstring_view(name).substr(12);auto separator=numbers.find(L'-');
  auto digits=[](std::wstring_view value){return !value.empty()&&std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';});};
  if(separator==std::wstring_view::npos||!digits(numbers.substr(0,separator))||!digits(numbers.substr(separator+1)))return {};
  const DWORD attributes=GetFileAttributesW(directory.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
  std::ifstream owner(directory/L"provider-launch-diagnostic-owner.txt");char recorded[16385]{};owner.getline(recorded,sizeof(recorded));
  if(!owner||std::string(recorded)!=utf8(shortcut.native()))return {};
  return directory/L"provider-launch-diagnostic.txt";
 }catch(...){return {};}
}
void writeLaunchTrace(const std::filesystem::path& path,const std::string& text,bool append){
 Handle file(CreateFileW(path.c_str(),GENERIC_WRITE|FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 BY_HANDLE_FILE_INFORMATION information{};DWORD written=0;
 if(!file||!GetFileInformationByHandle(file.value,&information)||(information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||text.size()>4096)return;
 LARGE_INTEGER zero{};if(!SetFilePointerEx(file.value,zero,nullptr,append?FILE_END:FILE_BEGIN)||(!append&&!SetEndOfFile(file.value)))return;
 // Closing the handle publishes cache-backed bytes without adding durable disk I/O to the measured call.
 if(!WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size())return;
}
ProviderWireReply diagnosticOperation(ProviderOperation operation,const ProviderBytes& data){
 if(operation!=ProviderOperation::Launch)return runProviderOperation(operation,data);
 auto request=decodeLaunch(data);auto trace=request?ownedLaunchTrace(request->target):std::nullopt;
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};SetLastError(ERROR_SUCCESS);
 const BOOL queried=QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&limits,sizeof(limits),nullptr);const DWORD queryError=GetLastError();
 const auto begin=GetTickCount64();
 if(trace){std::ostringstream output;output<<"launch begin providerPID="<<GetCurrentProcessId()<<" jobQuery="<<queried<<" immediateJobError="<<queryError<<" jobFlags="<<limits.BasicLimitInformation.LimitFlags<<"\n";writeLaunchTrace(*trace,output.str(),false);}
 auto reply=runProviderOperation(operation,data);const DWORD observedError=GetLastError();const auto elapsed=GetTickCount64()-begin;
 if(trace){std::ostringstream output;output<<"launch end status="<<reply.status<<" elapsed="<<elapsed<<" ms observedPostReplyLastError="<<observedError<<" (not guaranteed to be the exact ShellExecute error)\n";writeLaunchTrace(*trace,output.str(),true);}
 return reply;
}
struct Fixture {
 std::filesystem::path directory,shortcut,marker;std::wstring eventName;Handle done,child;
 Fixture(){directory=std::filesystem::temp_directory_path()/(L"AetherShell-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(directory);shortcut=directory/L"Tea launch.lnk";marker=directory/L"launched.txt";eventName=L"Local\\Aether.Shell.Done."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());done.reset(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));
  ComPtr<IShellLinkW> link;ComPtr<IPersistFile> file;HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
  const auto arguments=L"--owned-marker \""+eventName+L"\" \""+marker.native()+L"\" \"quoted value\"";
  if(FAILED(hr)||FAILED(link->SetPath(executablePath().c_str()))||FAILED(link->SetArguments(arguments.c_str()))||FAILED(link->SetWorkingDirectory(directory.c_str()))||FAILED(link.As(&file))||FAILED(file->Save(shortcut.c_str(),TRUE)))throw std::runtime_error("Cannot author owned shortcut fixture.");
  std::ofstream owner(directory/L"provider-launch-diagnostic-owner.txt");owner<<utf8(shortcut.native())<<'\n';
 }
 bool observe(){return waitFor([&]{std::ifstream input(marker);DWORD pid=0;std::string cwd,arg;if(!(input>>pid))return false;input.ignore();std::getline(input,cwd);std::getline(input,arg);if(arg!="quoted value")return false;if(!child)child.reset(OpenProcess(SYNCHRONIZE,FALSE,pid));return static_cast<bool>(child);});}
 void diagnostics(const char* label,bool observed){
  std::error_code error;const bool present=std::filesystem::exists(marker,error);
  std::cout<<label<<" markerObserved="<<observed<<" markerPresent="<<present<<" markerError="<<error.value()<<" childOpened="<<static_cast<bool>(child)<<" childWait="<<(child?WaitForSingleObject(child.value,0):WAIT_FAILED)<<"\n";
  auto print=[&](const std::filesystem::path& path,const char* kind){std::ifstream input(path,std::ios::binary);char bytes[4096]{};input.read(bytes,sizeof(bytes));std::cout<<label<<' '<<kind<<" bytes="<<input.gcount()<<"\n";std::cout.write(bytes,input.gcount());std::cout<<'\n';};
  print(marker,"marker");print(directory/L"provider-launch-diagnostic.txt","provider trace");
 }
 ~Fixture(){if(done)SetEvent(done.value);if(child)WaitForSingleObject(child.value,3000);std::error_code error;std::filesystem::remove_all(directory,error);}
};
LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_APP+201){ComPtr<IPropertyStore> properties;PROPVARIANT empty{};if(FAILED(SHGetPropertyStoreForWindow(h,IID_PPV_ARGS(&properties)))||FAILED(properties->SetValue(PKEY_AppUserModel_ID,empty))||FAILED(properties->Commit()))return 0;NotifyWinEvent(EVENT_OBJECT_DESTROY,h,OBJID_WINDOW,CHILDID_SELF);NotifyWinEvent(EVENT_OBJECT_CREATE,h,OBJID_WINDOW,CHILDID_SELF);return 1;}
 if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
std::optional<ShellCompletion> completion(ShellRequests& service,std::uint64_t id,DWORD ms=10000){std::optional<ShellCompletion> result;waitFor([&]{for(auto& item:service.takeCompletions())if(item.id==id)result=std::move(item);return result.has_value();},ms);return result;}
void wireTests(){
 WindowMetadataRequest token{0x1234,0x5678,9,10};auto bytes=encodeWindowMetadataRequests({token});auto decoded=decodeWindowMetadataRequests(bytes);
 check(decoded&&decoded->size()==1&&decoded->front()==token,"metadata wire preserves HWND PID process birth and lifecycle epoch");
 bytes.push_back(0);check(!decodeWindowMetadataRequests(bytes),"metadata wire rejects trailing data");check(encodeWindowMetadataRequests(std::vector<WindowMetadataRequest>(129,token)).empty(),"metadata batches capped at128");
 auto response=encodeWindowMetadata({WindowMetadata{token,L"aumid:owned",true,false}});auto values=decodeWindowMetadata(response);check(values&&values->front().identity==L"aumid:owned"&&values->front().desktopKnown&&!values->front().onCurrentDesktop,"window identity and desktop status roundtrip");
 response.back()=0xff;check(!decodeWindowMetadata(response),"metadata wire rejects unknown flags");
 auto launch=encodeLaunch({L"C:\\Owned path\\Tea.lnk",0,0,0,false});auto target=decodeLaunch(launch);check(target&&target->target==L"C:\\Owned path\\Tea.lnk"&&!target->activate,"launch target preserves shortcut path and foreground policy");launch.pop_back();check(!decodeLaunch(launch),"launch wire rejects truncated payload");
 check(encodeLaunch({std::wstring(4097,L'x')}).empty(),"launch target length bounded");
 auto pin=decodePin(encodePin({"Tea","C:/Owned.lnk","aumid:owned"}));check(pin&&pin->identity=="aumid:owned","pin name target and identity roundtrip");
}
void launchLifetime(bool abrupt){
 Fixture fixture;auto options=selfOptions();options.launchBreakaway=true;ProviderClient client(options);
 if(!abrupt)check(ownedLaunchTrace(fixture.shortcut.native()).has_value(),"owned launch trace recognizes the exact fixture path before native invocation");
 const auto begin=GetTickCount64();auto reply=client.call(ProviderOperation::Launch,encodeLaunch({fixture.shortcut.native()}),nullptr,options.launchTimeoutMs);const auto replyElapsed=GetTickCount64()-begin;
 DWORD helper=client.processId();const auto observationBegin=GetTickCount64();bool observed=fixture.observe();const auto observationElapsed=GetTickCount64()-observationBegin;
 Handle process(OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,helper));const DWORD processError=process?ERROR_SUCCESS:GetLastError();
 std::cout<<(abrupt?"Abrupt launch":"Normal launch")<<" outcome="<<static_cast<int>(reply.outcome)<<" replyElapsed="<<replyElapsed<<" ms providerPID="<<helper<<" observationElapsed="<<observationElapsed<<" ms helperOpened="<<static_cast<bool>(process)<<" helperOpenError="<<processError<<" childWaitBeforeCleanup="<<(fixture.child?WaitForSingleObject(fixture.child.value,0):WAIT_FAILED)<<"\n";
 if(abrupt&&process)TerminateProcess(process.value,17);client.close();
 fixture.diagnostics(abrupt?"Abrupt launch":"Normal launch",observed);
 check(reply.outcome==ProviderOutcome::Ok&&observed&&WaitForSingleObject(fixture.child.value,0)==WAIT_TIMEOUT,abrupt?"owned launch child survives abrupt helper termination":"owned launch child survives normal helper cleanup");
 std::ifstream input(fixture.marker);DWORD pid=0;std::string cwd,arg;input>>pid;input.ignore();std::getline(input,cwd);std::getline(input,arg);
 check(arg=="quoted value"&&lower(wide(cwd))==lower(fixture.directory.native()),"shortcut arguments and working directory preserved by isolated Shell launch");
 check(!process||WaitForSingleObject(process.value,1000)==WAIT_OBJECT_0,"owned launch helper is retired");
}
void parentExitLifetime(){
 Fixture fixture;std::wstring command=L"\""+executablePath().native()+L"\" --abandon-launch \""+fixture.shortcut.native()+L"\"";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};
 const auto begin=GetTickCount64();SetLastError(ERROR_SUCCESS);
 bool launched=CreateProcessW(executablePath().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created)!=FALSE;const DWORD createError=GetLastError();Handle process(created.hProcess),thread(created.hThread);
 const DWORD waited=launched?WaitForSingleObject(process.value,10000):WAIT_FAILED;const DWORD waitError=waited==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
 bool exited=launched&&waited==WAIT_OBJECT_0;DWORD code=1;const BOOL gotExitCode=exited?GetExitCodeProcess(process.value,&code):FALSE;
 const auto parentElapsed=GetTickCount64()-begin;bool observed=fixture.observe();
 std::cout<<"Parent abandon launched="<<launched<<" immediateCreateError="<<createError<<" parentPID="<<created.dwProcessId<<" wait="<<waited<<" waitError="<<waitError<<" exitCodeKnown="<<gotExitCode<<" exitCode="<<code<<" parentElapsed="<<parentElapsed<<" ms\n";
 fixture.diagnostics("Parent abandon",observed);check(exited&&code==0&&observed&&WaitForSingleObject(fixture.child.value,0)==WAIT_TIMEOUT,"owned launched app survives abrupt parent exit without destructors");
}
void serviceTests(){
 const auto name=L"Local\\Aether.Shell.Stall."+std::to_wstring(GetCurrentProcessId());Handle entered(CreateEventW(nullptr,TRUE,FALSE,name.c_str()));
 auto options=selfOptions();options.pinTimeoutMs=60000;options.mode=L"stall-pin:"+name;ShellRequests service(options);service.start(nullptr);auto begin=GetTickCount64();auto pinId=service.requestPin(executablePath(),41);
 check(pinId&&GetTickCount64()-begin<250,"pin request enqueues without blocking caller");check(WaitForSingleObject(entered.value,3000)==WAIT_OBJECT_0,"owned pin provider actually enters blocked operation");
 Fixture fixture;const auto launchBegin=GetTickCount64();auto launchId=service.requestLaunch(nullptr,fixture.shortcut.native());auto launched=completion(service,launchId);const auto launchElapsed=GetTickCount64()-launchBegin;
 const bool observed=launched&&launched->outcome==ProviderOutcome::Ok&&fixture.observe();
 std::cout<<"Independent launch id="<<launchId<<" outcome="<<(launched?static_cast<int>(launched->outcome):-1)<<" replyElapsed="<<launchElapsed<<" ms observationAttempted="<<(launched&&launched->outcome==ProviderOutcome::Ok)<<"\n";
 fixture.diagnostics("Independent launch",observed);
 check(launched&&launched->outcome==ProviderOutcome::Ok&&observed,"blocked shortcut metadata cannot delay separate launch worker");
 begin=GetTickCount64();service.stop();check(GetTickCount64()-begin<1800,"service shutdown cancels blocked Shell operation with bounded wait");check(service.requestLaunch(nullptr,fixture.shortcut.native())==0&&service.requestPin(executablePath())==0,"stopped service rejects work");
 options=selfOptions();options.mode=L"stall-pin:"+name;options.pinTimeoutMs=10000;ShellRequests bounded(options);bounded.start(nullptr);std::size_t admitted=0;for(int i=0;i<100;++i)if(bounded.requestPin(executablePath(),i))++admitted;check(admitted==64,"pin admission caps total outstanding requests including in-flight work");bounded.stop();
 ShellRequests real(selfOptions());real.start(nullptr);const auto resolutionBegin=GetTickCount64();const auto resolutionId=real.requestPin(fixture.shortcut,72);auto resolved=completion(real,resolutionId);
 std::cout<<"Pin resolution id="<<resolutionId<<" elapsed="<<GetTickCount64()-resolutionBegin<<" ms outcome="<<(resolved?static_cast<int>(resolved->outcome):-1)<<" revision="<<(resolved?resolved->revision:0)<<" target="<<(resolved&&resolved->pin?resolved->pin->target:"<none>")<<"\n";
 check(resolved&&resolved->outcome==ProviderOutcome::Ok&&resolved->pin&&resolved->revision==72&&resolved->pin->target==utf8(fixture.shortcut.native()),"real async shortcut resolution retains request revision and launch target");real.stop();
 // Timeout produces one completion and never retries an uncertain launch.
 options=selfOptions();options.mode=L"stall-launch:"+name;options.launchTimeoutMs=150;ShellRequests timeout(options);timeout.start(nullptr);auto uncertain=completion(timeout,timeout.requestLaunch(nullptr,fixture.shortcut.native()));Sleep(250);check(uncertain&&uncertain->outcome==ProviderOutcome::Timeout&&timeout.takeCompletions().empty(),"uncertain timed-out launch produces one result without auto-retry");timeout.stop();
}
void blockedMetadata(){
 const auto name=L"Local\\Aether.Shell.MetadataStall."+std::to_wstring(GetCurrentProcessId());Handle entered(CreateEventW(nullptr,TRUE,FALSE,name.c_str()));auto options=selfOptions();options.mode=L"stall-metadata:"+name;options.metadataTimeoutMs=10000;
 ShellRequests service(options);service.start(nullptr);bool accepted=service.requestMetadata({WindowMetadataRequest{1,1,1,GetCurrentProcessId()}});
 check(accepted&&WaitForSingleObject(entered.value,2500)==WAIT_OBJECT_0,"metadata service actually enters blocked owned provider operation");auto begin=GetTickCount64();service.stop();check(GetTickCount64()-begin<1800,"blocked window-metadata provider cancels promptly on service stop");
}void metadataTests(){
 std::wstring command=L"\""+executablePath().native()+L"\" --owned-window";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};bool launched=CreateProcessW(executablePath().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&created)!=FALSE;Handle process(created.hProcess),thread(created.hThread);
 HWND fixture=nullptr;waitFor([&]{fixture=FindWindowW(L"Aether.Shell.MetadataFixture",L"Owned Shell metadata fixture");DWORD pid=0;if(fixture)GetWindowThreadProcessId(fixture,&pid);return pid==created.dwProcessId&&IsWindowVisible(fixture);});
 WindowCollector collector;auto begin=GetTickCount64();collector.snapshot();auto requests=collector.takeMetadataRequests();std::erase_if(requests,[&](const auto& token){return token.hwnd!=reinterpret_cast<std::uintptr_t>(fixture);});
 check(launched&&fixture&&GetTickCount64()-begin<1000&&requests.size()==1,"fast collector discovers fixture and emits metadata request without Shell COM");
 if(!requests.empty()){
  ProviderClient provider(selfOptions());auto result=provider.call(ProviderOperation::WindowMetadata,encodeWindowMetadataRequests(requests),nullptr,4000);auto values=decodeWindowMetadata(result.bytes);
  check(result.outcome==ProviderOutcome::Ok&&values&&values->front().identity==L"aumid:aether.shell.fixture","owned metadata provider resolves actual window AUMID");
  if(values){auto stale=*values;collector.invalidateIdentity(fixture);check(!collector.acceptMetadata(stale),"lifecycle invalidation rejects prior epoch completion even same HWND/PID");collector.snapshot();auto fresh=collector.takeMetadataRequests();std::erase_if(fresh,[&](const auto& token){return token.hwnd!=reinterpret_cast<std::uintptr_t>(fixture);});
   if(!fresh.empty()){auto value=values->front();value.token=fresh.front();auto wrong=value;wrong.token.birth++;check(!collector.acceptMetadata({wrong}),"wrong process birth rejects stale metadata");wrong=value;wrong.token.pid++;check(!collector.acceptMetadata({wrong}),"wrong PID rejects stale metadata");check(collector.acceptMetadata({value}),"matching live process and current epoch accepts metadata");auto snapshot=collector.snapshot();check(std::any_of(snapshot.begin(),snapshot.end(),[&](const auto& window){return window.hwnd==fixture&&window.identity==L"aumid:aether.shell.fixture";}),"enriched identity appears in fast subsequent snapshot");
    collector.invalidateIdentity(fixture);auto retry=collector.takeMetadataRequests();std::erase_if(retry,[&](const auto& token){return token.hwnd!=reinterpret_cast<std::uintptr_t>(fixture);});if(!retry.empty()){collector.acceptMetadata({WindowMetadata{retry.front(),{}}});auto cached=collector.snapshot();check(std::any_of(cached.begin(),cached.end(),[&](const auto& window){return window.hwnd==fixture&&window.identity==L"aumid:aether.shell.fixture";}),"failed metadata preserves last-good identity");auto pending=collector.takeMetadataRequests();check(std::none_of(pending.begin(),pending.end(),[&](const auto& token){return token.hwnd==reinterpret_cast<std::uintptr_t>(fixture);}),"failed metadata does not trigger idle retry storm");}
   }
  }
 }
 if(fixture){
  WindowCollector reuse;reuse.snapshot();auto previous=reuse.takeMetadataRequests();std::erase_if(previous,[&](const auto& token){return token.hwnd!=reinterpret_cast<std::uintptr_t>(fixture);});
  if(!previous.empty()){
   WindowMetadata retired{previous.front(),L"aumid:retired.window.lifetime",true,false};reuse.acceptMetadata({retired});
   check(SendMessageW(fixture,WM_APP+201,0,0)==1,"owned same-HWND fixture clears AUMID and emits synthetic destroy/create lifecycle");
   // Simulated HWND reuse keeps the real handle/PID but begins a new cache lifetime.
   reuse.forgetWindow(fixture);auto beforeEnrichment=reuse.snapshot();auto replacement=reuse.takeMetadataRequests();std::erase_if(replacement,[&](const auto& token){return token.hwnd!=reinterpret_cast<std::uintptr_t>(fixture);});
   check(std::any_of(beforeEnrichment.begin(),beforeEnrichment.end(),[&](const auto& window){return window.hwnd==fixture&&window.identity!=retired.identity;}),"new HWND lifetime clears old AUMID and off-desktop membership before enrichment");
   check(!replacement.empty()&&replacement.front().epoch!=retired.token.epoch&&!reuse.acceptMetadata({retired}),"new HWND lifetime uses unique epoch and rejects prior queued completion");
   if(!replacement.empty()){
    ProviderClient provider(selfOptions());auto reply=provider.call(ProviderOperation::WindowMetadata,encodeWindowMetadataRequests(replacement),nullptr,4000);auto metadata=decodeWindowMetadata(reply.bytes);
    check(reply.outcome==ProviderOutcome::Ok&&metadata&&metadata->front().identity.empty(),"replacement fixture provider confirms empty real AUMID");
    if(metadata)reuse.acceptMetadata(*metadata);auto afterEnrichment=reuse.snapshot();
    check(std::any_of(afterEnrichment.begin(),afterEnrichment.end(),[&](const auto& window){return window.hwnd==fixture&&window.identity!=retired.identity;}),"new lifetime with no AUMID retains executable fallback rather than retired identity");
   }
  }
 } if(fixture)PostMessageW(fixture,WM_CLOSE,0,0);check(launched&&WaitForSingleObject(process.value,3000)==WAIT_OBJECT_0,"owned metadata fixture exits cooperatively");
}
}
int wmain(int argc,wchar_t** argv){
 if(argc==5&&std::wstring_view(argv[1])==L"--owned-marker"){
  std::ofstream output{std::filesystem::path(argv[3])};output<<GetCurrentProcessId()<<'\n'<<utf8(std::filesystem::current_path().native())<<'\n'<<utf8(argv[4])<<'\n';output.close();Handle done(OpenEventW(SYNCHRONIZE,FALSE,argv[2]));return done&&WaitForSingleObject(done.value,15000)==WAIT_OBJECT_0?0:2;
 }
 if(argc==3&&std::wstring_view(argv[1])==L"--abandon-launch"){
  auto options=selfOptions();options.launchBreakaway=true;ProviderClient provider(options);const auto begin=GetTickCount64();auto reply=provider.call(ProviderOperation::Launch,encodeLaunch({argv[2]}),nullptr,options.launchTimeoutMs);
  if(auto trace=ownedLaunchTrace(argv[2])){std::ostringstream output;output<<"parent abandon provider reply outcome="<<static_cast<int>(reply.outcome)<<" elapsed="<<GetTickCount64()-begin<<" ms providerPID="<<provider.processId()<<" parentPID="<<GetCurrentProcessId()<<"\n";writeLaunchTrace(*trace,output.str(),true);}
  ExitProcess(reply.outcome==ProviderOutcome::Ok?0:3);
 }
 if(argc==7&&std::wstring_view(argv[1])==L"--provider-channel")return runProviderHost(argc,argv,[](ProviderOperation op,const ProviderBytes& data,std::wstring_view mode)->ProviderWireReply{
  bool stall=(op==ProviderOperation::PinIdentity&&mode.rfind(L"stall-pin:",0)==0)||(op==ProviderOperation::Launch&&mode.rfind(L"stall-launch:",0)==0)||(op==ProviderOperation::WindowMetadata&&mode.rfind(L"stall-metadata:",0)==0);
  if(stall){auto colon=mode.find(L':');Handle entered(OpenEventW(EVENT_MODIFY_STATE,FALSE,std::wstring(mode.substr(colon+1)).c_str()));if(entered)SetEvent(entered.value);Sleep(INFINITE);}return diagnosticOperation(op,data);
 });
 ComApartment com;if(FAILED(com.result))return 2;
 if(argc==2&&std::wstring_view(argv[1])==L"--owned-window"){
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=fixtureProc;cls.lpszClassName=L"Aether.Shell.MetadataFixture";RegisterClassW(&cls);HWND h=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Owned Shell metadata fixture",WS_OVERLAPPEDWINDOW,60,60,400,240,nullptr,nullptr,cls.hInstance,nullptr);ComPtr<IPropertyStore> properties;PROPVARIANT value{};
  if(SUCCEEDED(SHGetPropertyStoreForWindow(h,IID_PPV_ARGS(&properties)))){InitPropVariantFromString(L"Aether.Shell.Fixture",&value);properties->SetValue(PKEY_AppUserModel_ID,value);properties->Commit();PropVariantClear(&value);}ShowWindow(h,SW_SHOWNOACTIVATE);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
 }
 if(argc!=1)return 2;
 try{wireTests();launchLifetime(false);launchLifetime(true);parentExitLifetime();serviceTests();blockedMetadata();metadataTests();}catch(const std::exception& error){check(false,error.what());}
 std::cout<<checks-failures<<'/'<<checks<<" Shell request checks passed\n";return failures?1:0;
}