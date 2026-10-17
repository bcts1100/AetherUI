#include "platform/safety.hpp"
#include <iostream>
#include <new>

namespace {
// Each case uses an UNARMED snapshot and event as the parent. No taskbar
// visibility or Explorer appbar state can be changed by this test.
bool handshake(bool requestStop){
 using namespace aether::win;
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
 Handle parent(CreateEventW(&sa,TRUE,FALSE,nullptr));
 Handle ready(CreateEventW(&sa,TRUE,FALSE,nullptr));
 Handle stop(CreateEventW(&sa,TRUE,FALSE,nullptr));
 Handle commandEvent(CreateEventW(&sa,FALSE,FALSE,nullptr));
 Handle completed(CreateEventW(&sa,FALSE,FALSE,nullptr));
 Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(RecoverySnapshot),nullptr));
 if(!parent||!ready||!stop||!mapping||!commandEvent||!completed)return false;
 auto* snapshot=static_cast<RecoverySnapshot*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecoverySnapshot)));
 if(!snapshot)return false;
 new(snapshot) RecoverySnapshot{};
 auto exe=executablePath().parent_path()/L"AetherWatchdog.exe";
 auto text=[](HANDLE h){return std::to_wstring(reinterpret_cast<std::uintptr_t>(h));};
 std::wstring command=L"\""+exe.wstring()+L"\" "+text(parent.value)+L" "+text(mapping.value)+L" "+text(ready.value)+L" "+text(stop.value)+L" "+text(commandEvent.value)+L" "+text(completed.value);
 STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){
  UnmapViewOfFile(snapshot);return false;
 }
 CloseHandle(process.hThread);Handle child(process.hProcess);
 HANDLE startupWait[]{ready.value,child.value};
 bool acknowledged=WaitForMultipleObjects(2,startupWait,FALSE,5000)==WAIT_OBJECT_0;
 bool alive=WaitForSingleObject(child.value,0)==WAIT_TIMEOUT;
 bool untouched=snapshot->armed==0&&snapshot->count==0;
 InterlockedExchange(&snapshot->command,static_cast<LONG>(RecoveryCommand::Arm));InterlockedExchange(&snapshot->commandSequence,1);SetEvent(commandEvent.value);
 bool denied=WaitForSingleObject(completed.value,2000)==WAIT_OBJECT_0&&snapshot->acknowledgedSequence==1&&snapshot->commandSucceeded==0&&snapshot->armed==0;
 if(requestStop)SetEvent(stop.value);else SetEvent(parent.value);
 bool exited=WaitForSingleObject(child.value,5000)==WAIT_OBJECT_0;
 DWORD code=99;if(exited)GetExitCodeProcess(child.value,&code);
 bool parentUntouched=!requestStop||WaitForSingleObject(parent.value,0)==WAIT_TIMEOUT;
 // Cleanup remains cooperative, even if a stop-event regression occurs.
 // The child holds only an unarmed snapshot, so it cannot modify taskbars.
 if(!exited){SetEvent(parent.value);SetEvent(stop.value);WaitForSingleObject(child.value,2000);}
 bool stillUntouched=snapshot->armed==0&&snapshot->count==0;
 UnmapViewOfFile(snapshot);
 if(!acknowledged||!alive||!untouched||!denied||!exited||code!=0||!parentUntouched||!stillUntouched){
  std::cerr<<(requestStop?"Graceful stop":"Parent exit")<<" handshake failed: acknowledged="<<acknowledged
   <<", permission-denied="<<denied<<", alive="<<alive<<", exited="<<exited<<", exit code="<<code<<".\n";
  return false;
 }
 return true;
}
}
int main(){
 if(!handshake(false))return 1;
 if(!handshake(true))return 2;
 std::cout<<"Helper acknowledged and exited for parent exit and graceful stop; unarmed snapshots stayed unchanged and unpermitted Arm commands were denied.\n";
 return 0;
}
