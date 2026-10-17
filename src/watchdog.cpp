#include "platform/safety.hpp"
#include "platform/recovery_engine.hpp"
#include "platform/config.hpp"
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <limits>
namespace {
aether::win::RecoverySnapshot* eventSnapshot=nullptr;HANDLE eventRefresh=nullptr;
void helperDiagnostic(const char* reason,DWORD event=0,std::uint64_t hwnd=0,LONG command=0,LONG sequence=0,LONG succeeded=0) noexcept {
 try {std::ostringstream out;out<<"Taskbar helper DIAG reason="<<reason<<" process="<<GetCurrentProcessId()<<" tick="<<GetTickCount64()<<" event="<<event<<" hwnd="<<hwnd<<" command="<<command<<" sequence="<<sequence<<" succeeded="<<succeeded;
  if(eventSnapshot)out<<" armed="<<InterlockedCompareExchange(&eventSnapshot->armed,0,0)<<" revoked="<<InterlockedCompareExchange(&eventSnapshot->revoked,0,0)<<" phase="<<InterlockedCompareExchange(&eventSnapshot->phase,0,0)<<" count="<<InterlockedCompareExchange(&eventSnapshot->count,0,0);
  aether::win::log(out.str());
 }catch(...){}
}
void CALLBACK taskbarEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG child,DWORD,DWORD){
 if(!window||object!=OBJID_WINDOW||child!=CHILDID_SELF||!eventSnapshot||!eventRefresh)return;
 if(event==EVENT_OBJECT_DESTROY){
  if(aether::win::observeRecoveryWindowEvent(eventSnapshot,event,reinterpret_cast<std::uintptr_t>(window))){helperDiagnostic("lifecycle-revoked",event,reinterpret_cast<std::uintptr_t>(window));SetEvent(eventRefresh);}
 }else {
  const bool observed=aether::win::observeRecoveryWindowEvent(eventSnapshot,event,reinterpret_cast<std::uintptr_t>(window));
  if(observed)helperDiagnostic("lifecycle-revoked",event,reinterpret_cast<std::uintptr_t>(window));
  if(observed||aether::win::nativeTaskbarWindow(window))SetEvent(eventRefresh);
 }
}
LONG load(const volatile LONG* value){return InterlockedCompareExchange(const_cast<volatile LONG*>(value),0,0);}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
 using namespace aether::win;
 int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 1;
 if(count==2&&wcscmp(args[1],L"--self-test")==0){RecoverySnapshot s{};LocalFree(args);return recoverySnapshotValid(&s)&&s.armed==0?0:2;}
 if(count!=7){LocalFree(args);return 3;}
 auto parse=[](const wchar_t* s)->HANDLE{
  if(!s||!*s)return nullptr;for(const wchar_t* c=s;*c;++c)if(*c<L'0'||*c>L'9')return nullptr;
  wchar_t* end=nullptr;errno=0;auto value=wcstoull(s,&end,10);
  return errno!=ERANGE&&end&&*end==L'\0'&&value&&value<=std::numeric_limits<std::uintptr_t>::max()?reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value)):nullptr;
 };
 Handle parent(parse(args[1])),mapping(parse(args[2])),ready(parse(args[3])),stop(parse(args[4])),command(parse(args[5])),completed(parse(args[6]));
 LocalFree(args);if(!parent||!mapping||!ready||!stop||!command||!completed)return 4;
 auto* snapshot=static_cast<RecoverySnapshot*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecoverySnapshot)));if(!snapshot)return 5;
 if(!recoverySnapshotValid(snapshot)||(snapshot->nativeAllowed==0&&load(&snapshot->armed)!=0)){UnmapViewOfFile(snapshot);return 5;}
 // Capture the entry role before Ready: normal restoration later revokes too.
 // An armed, already-revoked mapping identifies a restore-only successor.
 const auto recoveryContext=load(&snapshot->armed)!=0&&load(&snapshot->revoked)!=0
  ?NativeRecoveryContext::RestoreOnlyHelper:NativeRecoveryContext::ActiveHelper;
 Handle refresh(CreateEventW(nullptr,FALSE,FALSE,nullptr));if(!refresh){UnmapViewOfFile(snapshot);return 6;}
 HANDLE waits[]{parent.value,stop.value,command.value,refresh.value};
 if(WaitForMultipleObjects(4,waits,FALSE,0)==WAIT_FAILED){UnmapViewOfFile(snapshot);return 7;}
 // Hook callbacks only signal coalesced refresh or invalidate a destroyed
 // identity. Native enumeration/hide runs in this sole writer afterwards.
 eventSnapshot=snapshot;eventRefresh=refresh.value;
 HWINEVENTHOOK hook=snapshot->nativeAllowed?SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_SHOW,nullptr,taskbarEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS):nullptr;
 if(snapshot->nativeAllowed&&!hook){eventSnapshot=nullptr;eventRefresh=nullptr;UnmapViewOfFile(snapshot);return 6;}
 InterlockedExchange64(&snapshot->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
 if(!SetEvent(ready.value)){if(hook)UnhookWinEvent(hook);eventSnapshot=nullptr;eventRefresh=nullptr;UnmapViewOfFile(snapshot);return 6;}
 DWORD result=WAIT_TIMEOUT;bool restore=false,pendingRefresh=false;ULONGLONG settleUntil=0,settleBegan=0;
 for(;;){
  InterlockedExchange64(&snapshot->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
  // Lifetime/stop and lease revocation precede every pending hide command.
  const DWORD lifetime=WaitForMultipleObjects(2,waits,FALSE,0);
  if(lifetime==WAIT_OBJECT_0||lifetime==WAIT_OBJECT_0+1){result=lifetime;restore=true;break;}
  if(lifetime==WAIT_FAILED){result=WAIT_FAILED;restore=true;break;}
  if(load(&snapshot->revoked)){result=WAIT_OBJECT_0;restore=true;break;}
  if(recoveryLeaseExpired(snapshot,GetTickCount64())){
   // Read-only breadcrumbs identify the stalled UI work without renewing its
   // lease or waiting for that UI thread to answer a message.
   auto controller=FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller");DWORD owner=0;GetWindowThreadProcessId(controller,&owner);
   if(controller&&owner==GetProcessId(parent.value)){
    std::ostringstream trace;trace<<"UI lease expiry DIAG parent="<<owner
     <<" messagePlusOne="<<reinterpret_cast<ULONG_PTR>(GetPropW(controller,L"Aether.UI.Message"))
     <<" wParam="<<reinterpret_cast<ULONG_PTR>(GetPropW(controller,L"Aether.UI.WParam"))
     <<" dispatchStarted="<<reinterpret_cast<ULONG_PTR>(GetPropW(controller,L"Aether.UI.Started"));
    aether::win::log(trace.str());
   }
   helperDiagnostic("controller-lease-expired");result=WAIT_OBJECT_0;restore=true;break;
  }
  if(settleUntil&&!maintainRecoveryVisibility(snapshot,parent.value,stop.value)){restore=true;break;}
  if(settleUntil&&GetTickCount64()>=settleUntil){settleUntil=0;settleBegan=0;pendingRefresh=true;}
  if(pendingRefresh&&!settleUntil&&snapshot->nativeAllowed&&load(&snapshot->armed)){
   pendingRefresh=false;
   if(!runNativeRecoveryCommand(snapshot,RecoveryCommand::Refresh,GetTickCount64(),parent.value,stop.value))helperDiagnostic("settled-refresh-failed");
   if(load(&snapshot->revoked)){restore=true;break;}
  }
  result=MsgWaitForMultipleObjectsEx(4,waits,settleUntil?16:500,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
  if(result==WAIT_OBJECT_0||result==WAIT_OBJECT_0+1){restore=true;break;}
  if(result==WAIT_FAILED){restore=true;break;}
  if(result==WAIT_OBJECT_0+4){MSG message{};unsigned drained=0;while(drained++<128&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}continue;}
  if(result==WAIT_OBJECT_0+2){
   const LONG sequence=load(&snapshot->commandSequence);const auto request=static_cast<RecoveryCommand>(load(&snapshot->command));
   if(sequence!=load(&snapshot->acknowledgedSequence)){
    bool succeeded=false;
    if(request==RecoveryCommand::AppearanceSettle&&snapshot->nativeAllowed&&load(&snapshot->armed)&&!load(&snapshot->revoked)){
     const auto now=GetTickCount64();if(!settleBegan)settleBegan=now;
     if(now-settleBegan<6000){settleUntil=std::min(now+2500,settleBegan+6000);pendingRefresh=true;succeeded=true;}
    }else if(request==RecoveryCommand::Refresh&&settleUntil){pendingRefresh=true;succeeded=false;}
    else if(request==RecoveryCommand::Restore){settleUntil=0;settleBegan=0;pendingRefresh=false;if(snapshot->nativeAllowed)succeeded=runNativeRecoveryCommand(snapshot,request,GetTickCount64(),parent.value,stop.value);else if(load(&snapshot->armed)==0){InterlockedExchange(&snapshot->revoked,1);succeeded=true;}}
    else if(snapshot->nativeAllowed){settleUntil=0;settleBegan=0;pendingRefresh=false;succeeded=runNativeRecoveryCommand(snapshot,request,GetTickCount64(),parent.value,stop.value);}
    if(request==RecoveryCommand::Arm||request==RecoveryCommand::Refresh||request==RecoveryCommand::ConfirmHidden||request==RecoveryCommand::AppearanceSettle){
     const DWORD priority=WaitForMultipleObjects(2,waits,FALSE,0);
     if(priority!=WAIT_TIMEOUT||load(&snapshot->revoked)||recoveryLeaseExpired(snapshot,GetTickCount64())){
      InterlockedExchange(&snapshot->revoked,1);succeeded=false;
     }
    }
    // Publish the proof/result and fresh helper liveness before optional I/O.
    // This heartbeat never renews the controller's lease.
    InterlockedExchange64(&snapshot->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange(&snapshot->commandSucceeded,succeeded?1:0);InterlockedExchange(&snapshot->acknowledgedSequence,sequence);SetEvent(completed.value);
    // Dynamic snapshot fields in this log are current post-ACK diagnostics;
    // command/sequence/succeeded describe the result that was just published.
    if(request!=RecoveryCommand::Refresh||!succeeded)helperDiagnostic("command-ack-published",0,0,static_cast<LONG>(request),sequence,succeeded?1:0);
   }
  }else if(result==WAIT_OBJECT_0+3&&snapshot->nativeAllowed&&load(&snapshot->armed)&&!load(&snapshot->revoked)){
   // Visibility enforcement does not wait for theme/appbar settlement.
   if(!maintainRecoveryVisibility(snapshot,parent.value,stop.value)){restore=true;break;}
   if(settleUntil){pendingRefresh=true;}
   else if(!runNativeRecoveryCommand(snapshot,RecoveryCommand::Refresh,GetTickCount64(),parent.value,stop.value))helperDiagnostic("event-refresh-failed");
  }
  if(load(&snapshot->revoked)){restore=true;break;}
 }
 if(restore){
  InterlockedExchange(&snapshot->revoked,1);
  do {
   InterlockedExchange64(&snapshot->helperHeartbeat,static_cast<LONG64>(GetTickCount64()));
   if(snapshot->nativeAllowed)restoreSnapshot(snapshot,recoveryContext);
   if(load(&snapshot->armed)!=0)Sleep(500);
  }while(load(&snapshot->armed)!=0&&recoverySnapshotValid(snapshot));
 }
 if(hook)UnhookWinEvent(hook);eventSnapshot=nullptr;eventRefresh=nullptr;UnmapViewOfFile(snapshot);
 return result==WAIT_FAILED?7:0;
}
