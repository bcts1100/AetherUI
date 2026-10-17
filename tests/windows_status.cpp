#include "platform/status.hpp"
#include <iostream>
#include <functional>
using namespace aether::win;
namespace {
int checks=0;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<std::endl;}
std::optional<StatusCompletion> receive(StatusService& service,DWORD wait=4000,unsigned* pulses=nullptr){
 auto until=GetTickCount64()+wait;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_APP+100&&pulses)++*pulses;TranslateMessage(&m);DispatchMessageW(&m);}if(auto done=service.takeCompletion())return done;Sleep(5);}while(GetTickCount64()<until);return {};
}
}
int wmain(int argc,wchar_t** argv){
 if(argc>1)return runProviderHost(argc,argv,[](ProviderOperation operation,const ProviderBytes& bytes,std::wstring_view mode)->ProviderWireReply{
  if(operation!=ProviderOperation::Status)return {1,{}};
  if(mode==L"stall-status")Sleep(INFINITE);
  if(mode==L"malformed-status")return {0,{102,0,0}};
  if(bytes.size()!=2)return {1,{}};
  return {0,{static_cast<std::uint8_t>(bytes[0]==1?bytes[1]+1:43),static_cast<std::uint8_t>(bytes[0]==2?1:0),2}};
 });
 try{
  MSG initial{};PeekMessageW(&initial,nullptr,0,0,PM_NOREMOVE);
  ProviderProcessOptions options;options.executable=executablePath();options.metadataTimeoutMs=1500;options.idleMs=100;
  {StatusService status(options);status.start(nullptr);auto begin=GetTickCount64();status.request();check(GetTickCount64()-begin<100,"status request does not wait for COM/provider");auto result=receive(status);check(result&&result->outcome==ProviderOutcome::Ok&&result->value.volume==42&&result->value.network==L"Online","bounded provider result reaches UI cache");
   status.volume(73);result=receive(status);check(result&&result->value.volume==73,"volume command executes off UI thread");status.mute();result=receive(status);check(result&&result->value.muted,"mute command executes off UI thread");status.stop();status.request();check(!status.takeCompletion(),"stopped service does not publish stale data");}
  {options.mode=L"malformed-status";StatusService status(options);status.start(nullptr);status.request();auto result=receive(status);check(result&&result->outcome==ProviderOutcome::Malformed,"out-of-range external values rejected");}
  {options.mode=L"stall-status";options.metadataTimeoutMs=350;StatusService status(options);status.start(nullptr);auto begin=GetTickCount64();status.request();unsigned pulses=0;
   for(int n=0;n<12;++n)PostThreadMessageW(GetCurrentThreadId(),WM_APP+100,0,0);
   auto result=receive(status,3000,&pulses);check(result&&result->outcome==ProviderOutcome::Timeout&&GetTickCount64()-begin<2500,"hung audio/network provider times out and is retired");check(pulses==12,"UI keeps dispatching while status provider is hung");}
  {options.metadataTimeoutMs=15000;StatusService status(options);status.start(nullptr);status.request();Sleep(150);auto begin=GetTickCount64();status.stop();check(GetTickCount64()-begin<1000,"shutdown cancels hung status provider without waiting for its deadline");}
  check(!readStatus(true,false,false).clock.empty(),"local clock and root counters need no audio/network COM");
  std::cout<<checks<<" asynchronous status checks passed\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
