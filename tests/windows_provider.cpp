#include "platform/provider_client.hpp"
#include "platform/provider_codec.hpp"
#include <iostream>
#include <thread>
int checks=0,failures=0;
void check(bool ok,const char* name){++checks;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;}
bool gone(DWORD pid){if(!pid)return false;aether::win::Handle process(OpenProcess(SYNCHRONIZE,FALSE,pid));return !process||WaitForSingleObject(process.value,1500)==WAIT_OBJECT_0;}
int wmain(int argc,wchar_t** argv){
 using namespace aether::win;
 if(argc==3&&std::wstring_view(argv[1])==L"--abandon-provider"){
  Handle mapping(OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[2]));if(!mapping)return 2;
  auto* data=static_cast<DWORD*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,8));if(!data)return 2;
  ProviderProcessOptions options;options.executable=executablePath();options.mode=L"echo";
  ProviderClient client(options);auto reply=client.call(ProviderOperation::Icon,{1},nullptr,2000);
  data[0]=client.processId();data[1]=reply.outcome==ProviderOutcome::Ok?1:0;MemoryBarrier();
  UnmapViewOfFile(data);ExitProcess(0);
 }
 if(argc==7&&std::wstring_view(argv[6]).rfind(L"raw-",0)==0){
  Handle mapping(reinterpret_cast<HANDLE>(_wcstoui64(argv[2],nullptr,10)));
  Handle request(reinterpret_cast<HANDLE>(_wcstoui64(argv[3],nullptr,10)));
  Handle response(reinterpret_cast<HANDLE>(_wcstoui64(argv[4],nullptr,10)));
  auto* header=static_cast<std::uint32_t*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,24+ProviderPayloadLimit));
  if(!header||WaitForSingleObject(request.value,2000)!=WAIT_OBJECT_0)return 2;
  MemoryBarrier();std::wstring mode=argv[6];
  if(mode==L"raw-size")header[4]=static_cast<std::uint32_t>(ProviderPayloadLimit+1);
  else if(mode==L"raw-magic")++header[0];else if(mode==L"raw-version")++header[1];
  else if(mode==L"raw-operation")header[2]=3;else if(mode==L"raw-sequence")++header[3];
  MemoryBarrier();SetEvent(response.value);Sleep(3000);UnmapViewOfFile(header);return 0;
 }
 if(argc>1)return runProviderHost(argc,argv,[](ProviderOperation op,const ProviderBytes& data,std::wstring_view mode)->ProviderWireReply{
  if(mode.rfind(L"stall-all:",0)==0){
   Handle observed(OpenEventW(EVENT_MODIFY_STATE,FALSE,std::wstring(mode.substr(10)).c_str()));
   if(observed)SetEvent(observed.value);Sleep(INFINITE);
  }
  if(mode==L"stall"&&op==ProviderOperation::Icon)Sleep(INFINITE);
  if(mode==L"exit")ExitProcess(17);
  if(mode==L"exit-later")QueueUserWorkItem([](void*)->DWORD{Sleep(120);ExitProcess(0);},nullptr,WT_EXECUTEDEFAULT);
  if(mode==L"malformed")return {99,{}};
  if(mode==L"oversize")return {0,ProviderBytes(ProviderPayloadLimit+1,42)};
  if(mode==L"inherit"){
   if(data.size()==sizeof(std::uintptr_t)){std::uintptr_t value=0;memcpy(&value,data.data(),sizeof(value));SetEvent(reinterpret_cast<HANDLE>(value));}
  }
  if(op==ProviderOperation::Quit)return {};
  return {0,data};
 });
 ComApartment com;
 auto encoded=encodeApplications({{L"Owned app",L"shell:AppsFolder\\Owned!App",L"aumid:owned"},{L"Shortcut",L"C:\\Owned.lnk",L"c:/owned.exe"}});
 auto decoded=decodeApplications(encoded);check(decoded&&decoded->size()==2&&(*decoded)[0].name==L"Owned app"&&(*decoded)[1].target==L"C:\\Owned.lnk","catalog wire roundtrip preserves bounded names/targets/IDs");
 auto truncated=encoded;if(!truncated.empty())truncated.pop_back();check(!decodeApplications(truncated),"truncated catalog payload rejected");
 check(!decodeApplications(ProviderBytes(ProviderPayloadLimit+1)),"oversized catalog payload rejected");
 check(encodeProviderTarget(std::wstring(4097,L'x')).empty(),"oversized provider target refused");
 auto target=decodeProviderTarget(encodeProviderTarget(L"C:\\Owned app.lnk"));check(target&&*target==L"C:\\Owned app.lnk","icon target Unicode roundtrip");
 IconPixels icon;icon.width=128;icon.height=64;icon.bytes.assign(128*64*4,255);
 auto image=decodeIcon(encodeIcon(&icon));check(image&&image->width==128&&image->height==64&&image->bytes==icon.bytes,"bounded icon pixels roundtrip");
 icon.width=129;check(encodeIcon(&icon).empty(),"oversized icon dimensions refused");
 check(!decodeIcon({0xff,0xff,0xff,0xff}),"malformed icon payload rejected");
 Handle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));
 ProviderProcessOptions options;options.executable=executablePath();options.mode=L"echo";
 {
  ProviderClient client(options);auto reply=client.call(ProviderOperation::Icon,{1,2,3,4},stop.value,2000);DWORD pid=client.processId();
  check(reply.outcome==ProviderOutcome::Ok&&reply.bytes==ProviderBytes({1,2,3,4})&&pid!=0,"actual owned provider responds over inherited channel");
  auto again=client.call(ProviderOperation::Icon,{5,6},stop.value,2000);check(again.outcome==ProviderOutcome::Ok&&client.processId()==pid&&client.statistics().started==1,"burst reuses one provider");
  client.close();check(gone(pid),"idle provider closes cooperatively without orphan");
 }
 {
  options.mode=L"stall";ProviderClient client(options);auto start=GetTickCount64();
  auto warmup=client.call(ProviderOperation::Catalog,{},stop.value,2000);DWORD pid=client.processId();auto reply=client.call(ProviderOperation::Icon,{},stop.value,150);
  check(reply.outcome==ProviderOutcome::Timeout&&GetTickCount64()-start<1800&&client.statistics().timeouts==1,"actual blocked provider deadline bounds caller");
  check(warmup.outcome==ProviderOutcome::Ok&&client.processId()==0&&gone(pid),"timed-out known provider process exits and ownership retires");
 }
 {
  options.mode=L"stall";ProviderClient client(options);client.call(ProviderOperation::Catalog,{},stop.value,2000);DWORD pid=client.processId();std::thread cancel([&]{Sleep(150);SetEvent(stop.value);});auto start=GetTickCount64();
  auto reply=client.call(ProviderOperation::Icon,{},stop.value,10000);cancel.join();
  check(reply.outcome==ProviderOutcome::Cancelled&&GetTickCount64()-start<1800&&client.statistics().cancelled==1&&gone(pid),"stop event cancels actual blocked provider promptly");
  ResetEvent(stop.value);
 }
 {
  options.mode=L"exit";ProviderClient client(options);auto reply=client.call(ProviderOperation::Icon,{},stop.value,2000);
  check(reply.outcome==ProviderOutcome::Exited,"crashing provider reports failure without blocking UI");
 }
 {
  options.mode=L"malformed";ProviderClient client(options);auto reply=client.call(ProviderOperation::Icon,{},stop.value,2000);
  check(reply.outcome==ProviderOutcome::Malformed,"unknown response status rejected");
 }
 {
  options.mode=L"oversize";ProviderClient client(options);auto reply=client.call(ProviderOperation::Catalog,{},stop.value,2000);
  check(reply.outcome!=ProviderOutcome::Ok,"oversize reply never crosses fixed channel");
 }
 {
  SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};Handle sentinel(CreateEventW(&security,TRUE,FALSE,nullptr));
  options.mode=L"inherit";ProviderClient client(options);std::uintptr_t raw=reinterpret_cast<std::uintptr_t>(sentinel.value);ProviderBytes data(sizeof(raw));memcpy(data.data(),&raw,sizeof(raw));
  auto reply=client.call(ProviderOperation::Icon,data,stop.value,2000);
  check(reply.outcome==ProviderOutcome::Ok&&WaitForSingleObject(sentinel.value,0)==WAIT_TIMEOUT,"explicit inheritance excludes unrelated inheritable parent event");
 }
 {
  options.mode=L"echo";ProviderClient client(options);SetEvent(stop.value);auto reply=client.call(ProviderOperation::Catalog,{},stop.value,2000);
  check(reply.outcome==ProviderOutcome::Cancelled&&client.processId()==0,"pre-cancel avoids creating provider");ResetEvent(stop.value);
 }
 {
  const auto eventName=L"Local\\Aether.Catalog.ProviderTest."+std::to_wstring(GetCurrentProcessId());
  Handle observed(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));
  options.mode=L"stall-all:"+eventName;Catalog catalog(options);catalog.start(nullptr);
  bool started=WaitForSingleObject(observed.value,2500)==WAIT_OBJECT_0;
  auto begin=GetTickCount64();catalog.stop();
  check(started&&GetTickCount64()-begin<1800,"Catalog wrapper invokes owned provider and cancels blocked scan on stop");
 }
 {
  const auto eventName=L"Local\\Aether.Icon.ProviderTest."+std::to_wstring(GetCurrentProcessId());
  Handle observed(CreateEventW(nullptr,TRUE,FALSE,eventName.c_str()));
  options.mode=L"stall-all:"+eventName;IconCache icons(options);icons.start(nullptr);
  icons.request(L"owned-test",executablePath().native());
  bool started=WaitForSingleObject(observed.value,2500)==WAIT_OBJECT_0;
  auto begin=GetTickCount64();icons.stop();
  check(started&&GetTickCount64()-begin<1800,"IconCache wrapper invokes owned provider and cancels blocked load on stop");
 }
 {
  options.mode=L"exit-later";ProviderClient client(options);
  auto reply=client.call(ProviderOperation::Icon,{3},stop.value,2000);DWORD old=client.processId();
  bool exited=gone(old);auto next=client.call(ProviderOperation::Icon,{7},stop.value,2000);
  check(reply.outcome==ProviderOutcome::Ok&&exited&&next.outcome==ProviderOutcome::Ok&&next.bytes==ProviderBytes({7})&&client.processId()!=old&&client.statistics().started==2,"provider exited between bursts restarts before serving the next target");
 }
 for(const auto* mode:{L"raw-size",L"raw-magic",L"raw-version",L"raw-operation",L"raw-sequence"}){
  options.mode=mode;ProviderClient client(options);auto reply=client.call(ProviderOperation::Icon,{1},stop.value,2000);
  check(reply.outcome==ProviderOutcome::Malformed&&client.processId()==0,"raw malformed size/magic/version/operation/sequence retires provider before payload copy");
 }
 {
  const auto name=L"Local\\Aether.Abandon.ProviderTest."+std::to_wstring(GetCurrentProcessId());
  Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,8,name.c_str()));
  auto* data=static_cast<DWORD*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,8));
  std::wstring command=L"\""+executablePath().native()+L"\" --abandon-provider \""+name+L"\"";
  STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
  bool launched=CreateProcessW(executablePath().c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
  Handle process(child.hProcess),thread(child.hThread);bool exited=launched&&WaitForSingleObject(process.value,4000)==WAIT_OBJECT_0;
  MemoryBarrier();check(data&&exited&&data[0]&&data[1]==1&&gone(data[0]),"owned job closes provider when parent exits without running destructors");
  if(data)UnmapViewOfFile(data);
 }
 std::cout<<checks-failures<<'/'<<checks<<" provider checks passed\n";return failures?1:0;
}



