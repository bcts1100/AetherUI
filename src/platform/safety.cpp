#include "safety.hpp"
#include "recovery_engine.hpp"
#include "config.hpp"
#include <new>
#include <sstream>
namespace aether::win {
namespace {
LONG load(const volatile LONG* value) noexcept{return InterlockedCompareExchange(const_cast<volatile LONG*>(value),0,0);}
ULONGLONG ticks(const volatile LONG64* value) noexcept{return static_cast<ULONGLONG>(InterlockedCompareExchange64(const_cast<volatile LONG64*>(value),0,0));}
bool taskbarClass(HWND h) noexcept{wchar_t name[64]{};return GetClassNameW(h,name,64)&&(wcscmp(name,L"Shell_TrayWnd")==0||wcscmp(name,L"Shell_SecondaryTrayWnd")==0);}
ULONGLONG processBirth(DWORD pid) noexcept {
 Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));FILETIME created{},exit{},kernel{},user{};
 if(!process||!GetProcessTimes(process.value,&created,&exit,&kernel,&user))return 0;
 return (static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
}
bool explorerOwner(DWORD pid) noexcept {
 // A held live process handle pins this PID generation. Rechecking its image
 // for every HWND during a single Shell transition adds synchronous work but
 // no identity evidence. A dead handle or changed PID always reacquires.
 struct OwnerCache {Handle process;DWORD pid=0;bool genuine=false;};
 static thread_local OwnerCache cache;
 if(cache.pid==pid&&cache.genuine&&cache.process&&WaitForSingleObject(cache.process.value,0)==WAIT_TIMEOUT)return true;
 cache.process.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));cache.pid=pid;cache.genuine=false;
 if(!cache.process)return false;
 wchar_t actual[32768]{},directory[32768]{};DWORD length=32768;
 const UINT n=GetWindowsDirectoryW(directory,32768);
 if(!n||n>=32768||!QueryFullProcessImageNameW(cache.process.value,0,actual,&length))return false;
 try {const auto expected=(std::filesystem::path(directory)/L"explorer.exe").wstring();cache.genuine=_wcsicmp(actual,expected.c_str())==0;return cache.genuine;}catch(...){return false;}
}
std::vector<HWND> nativeBars(){
 std::vector<HWND> result;result.reserve(33);
 EnumWindows([](HWND h,LPARAM p)->BOOL{if(nativeTaskbarWindow(h)){auto& bars=*reinterpret_cast<std::vector<HWND>*>(p);bars.push_back(h);if(bars.size()==33)return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&result));
 // Explorer can omit a live primary or secondary taskbar from EnumWindows.
 // Query both live classes independently; never synthesize from saved handles.
 for(const auto* name:{L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd"}){
  HWND after=nullptr;for(int scanned=0;scanned<33;++scanned){auto h=FindWindowExW(nullptr,after,name,nullptr);if(!h)break;after=h;
   if(result.size()<33&&nativeTaskbarWindow(h)&&std::find(result.begin(),result.end(),h)==result.end())result.push_back(h);
  }
 }
 return result;
}
HWND primaryBar() noexcept {
 HWND primary=FindWindowW(L"Shell_TrayWnd",nullptr);if(nativeTaskbarWindow(primary))return primary;
 HWND result=nullptr;EnumWindows([](HWND h,LPARAM p)->BOOL{wchar_t name[64]{};if(GetClassNameW(h,name,64)&&wcscmp(name,L"Shell_TrayWnd")==0&&nativeTaskbarWindow(h)){*reinterpret_cast<HWND*>(p)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&result));return result;
}
void recoveryDiagnostic(const RecoverySnapshot& snapshot,const char* reason,DWORD error=0,bool nativeObservation=true) noexcept {
 try {
  std::ostringstream out;out<<"Taskbar recovery DIAG reason="<<reason<<" process="<<GetCurrentProcessId()<<" tick="<<GetTickCount64()
   <<" armed="<<load(&snapshot.armed)<<" revoked="<<load(&snapshot.revoked)<<" phase="<<load(&snapshot.phase)
   <<" count="<<load(&snapshot.count)<<" capacity=32 originalState="<<snapshot.taskbarState<<" requestedHiddenState="<<(snapshot.taskbarState|ABS_AUTOHIDE)
   <<" controllerHeartbeat="<<ticks(&snapshot.heartbeat)<<" helperHeartbeat="<<ticks(&snapshot.helperHeartbeat)<<" leaseMs="<<snapshot.leaseMilliseconds<<" immediateError="<<error;
  if(!nativeObservation){out<<" nativeObservation=not-requested";log(out.str());return;}
  APPBARDATA data{sizeof(data)};data.hWnd=primaryBar();if(data.hWnd)out<<" observedState="<<SHAppBarMessage(ABM_GETSTATE,&data);else out<<" observedState=unavailable";
  unsigned visible=0;const auto bars=nativeBars();for(HWND h:bars)if(IsWindowVisible(h))++visible;out<<" genuineBars="<<bars.size()<<" genuineVisible="<<visible;
  for(LONG i=0;i<std::clamp<LONG>(load(&snapshot.count),0,32);++i){const auto& entry=snapshot.entries[i];auto h=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));DWORD currentPid=0;GetWindowThreadProcessId(h,&currentPid);
   out<<" [entry="<<i<<" hwnd="<<entry.hwnd<<" originalPid="<<entry.pid<<" currentPid="<<currentPid<<" originalBirth="<<entry.processBirth<<" currentBirth="<<processBirth(currentPid)
    <<" originalVisible="<<entry.visible<<" currentVisible="<<(IsWindowVisible(h)?1:0)<<" genuine="<<(nativeTaskbarWindow(h)?1:0)<<"]";
  }log(out.str());
 }catch(...){log("Taskbar recovery diagnostic unavailable.");}
}
class NativeRecoveryBackend final:public RecoveryBackend,public RecoveryHideBackend,public RecoveryHiddenObservationBackend {
 struct Timing {ULONGLONG& total;ULONGLONG start=GetTickCount64();~Timing(){total+=GetTickCount64()-start;}};
 ULONGLONG captureMs_=0,prepareMs_=0,stateQueryMs_=0,visibilityScanMs_=0;
 struct HideTrace {ULONGLONG tick=0;std::uint64_t hwnd=0;DWORD targetThread=0,pid=0;LONG_PTR beforeStyle=0,afterStyle=0;BOOL beforeVisible=FALSE,afterVisible=FALSE,previousVisibleReturn=FALSE;bool attempted=false;};
 HideTrace hideTrace_[320]{};unsigned hideTraceCount_=0;
 bool initialCapture_=false;std::uint32_t newIdentities_=0;NativeRecoveryContext context_=NativeRecoveryContext::ControllerFallback;
 HANDLE confirmationParent_=nullptr,confirmationStop_=nullptr;bool confirmationInterrupted_=false;RecoverySnapshot* confirmationSnapshot_=nullptr;
 void flushHideTrace() noexcept {
  try {std::ostringstream out;out<<"Taskbar hide request DIAG process="<<GetCurrentProcessId()<<" helperThread="<<GetCurrentThreadId()<<" requestsObserved="<<hideTraceCount_
   <<" captureMs="<<captureMs_<<" prepareMs="<<prepareMs_<<" stateQueryMs="<<stateQueryMs_<<" visibilityScanMs="<<visibilityScanMs_;
   for(unsigned i=0;i<hideTraceCount_;++i){const auto& t=hideTrace_[i];out<<" [request="<<i<<" tick="<<t.tick<<" hwnd="<<t.hwnd<<" targetThread="<<t.targetThread<<" pid="<<t.pid<<" method=ShowWindow attempted="<<t.attempted<<" previousVisibleReturn="<<t.previousVisibleReturn<<" callCompleted=1 beforeVisible="<<t.beforeVisible<<" afterVisible="<<t.afterVisible<<" beforeStyle="<<t.beforeStyle<<" afterStyle="<<t.afterStyle<<"]";}
   log(out.str());
  }catch(...){}
 }
public:
 explicit NativeRecoveryBackend(NativeRecoveryContext context=NativeRecoveryContext::ControllerFallback,HANDLE parent=nullptr,HANDLE stop=nullptr):context_(context),confirmationParent_(parent),confirmationStop_(stop){}
 bool validEntry(const RecoveryEntry& entry) noexcept override {
  auto window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));
  return entry.processBirth&&windowAlive(window,entry.pid)&&processBirth(entry.pid)==entry.processBirth&&nativeTaskbarWindow(window);
 }
 void requestVisibility(const RecoveryEntry& entry) noexcept override {
  if(!validEntry(entry))return;
  const auto window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));const int target=entry.visible?SW_SHOWNA:SW_HIDE;
  // Synchronous restoration is confined to the independent writer. Its BOOL
  // is prior visibility, not success; state/visibility/workspace still confirm.
  // Controller fallback retains its asynchronous visibility behavior.
  const bool helperContext=context_!=NativeRecoveryContext::ControllerFallback;
  const BOOL result=helperContext?ShowWindow(window,target):ShowWindowAsync(window,target);
  try {std::ostringstream out;out<<"Taskbar restore visibility DIAG process="<<GetCurrentProcessId()<<" tick="<<GetTickCount64()
   <<" method="<<(helperContext?"ShowWindow":"ShowWindowAsync")<<" helperContext="<<helperContext<<" recoveryContext="<<static_cast<unsigned>(context_)<<" quietMs="<<restorationQuietMilliseconds()
   <<" hwnd="<<entry.hwnd<<" originalPid="<<entry.pid<<" originalBirth="<<entry.processBirth<<" originalVisible="<<entry.visible
   <<" returned="<<result<<" returnMeaning="<<(helperContext?"previousVisibility":"operationStarted");log(out.str());}catch(...){}
 }
 bool visibilityRestored(const RecoveryEntry& entry) noexcept override {
  return !validEntry(entry)||(IsWindowVisible(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd)))!=FALSE)==(entry.visible!=0);
 }
 void requestState(DWORD state) noexcept override {
  const DWORD setterState=nativeTaskbarSetterMask(state);
  // Restore each genuine current shell bar, including windows recreated while
  // recovery was pending. The global GETSTATE result ignores this target HWND.
  EnumWindows([](HWND h,LPARAM value)->BOOL{
   if(nativeTaskbarWindow(h)){APPBARDATA data{sizeof(data)};data.hWnd=h;data.lParam=static_cast<LPARAM>(*reinterpret_cast<const DWORD*>(value));SHAppBarMessage(ABM_SETSTATE,&data);}
   return TRUE;
  },reinterpret_cast<LPARAM>(&setterState));
 }
 bool stateRestored(DWORD state) noexcept override {
  // No Explorer does not confirm restoration of its persistent appbar bits.
  APPBARDATA data{sizeof(data)};data.hWnd=primaryBar();return data.hWnd&&SHAppBarMessage(ABM_GETSTATE,&data)==state;
 }
 void delay(DWORD milliseconds) noexcept override {if(confirmationSnapshot_)delayConfirmation(milliseconds);else Sleep(milliseconds);}
 bool hideAllowed(const RecoverySnapshot& snapshot) const noexcept override {
  return confirmationAllowed()&&!recoveryLeaseExpired(&snapshot,GetTickCount64())&&!load(&snapshot.revoked);
 }
 DWORD restorationQuietMilliseconds() const noexcept override {return context_==NativeRecoveryContext::ActiveHelper?100:1000;}
 bool captureWorkAreaOriginals(WorkAreaSnapshot& original) noexcept override {return captureWorkAreas(original);}
 WorkAreaResult restoreOriginalWorkAreas(const WorkAreaSnapshot& original) noexcept override {return restoreWorkAreas(original);}
 WorkAreaResult originalWorkAreasRestored(const WorkAreaSnapshot& original) noexcept override {
  WorkAreaSnapshot current;return captureWorkAreas(current)?compareWorkAreas(original,current):WorkAreaResult::Unavailable;
 }
 bool captureOriginals(RecoverySnapshot& snapshot) noexcept override {
  Timing timing{captureMs_};
  initialCapture_=load(&snapshot.armed)==0;newIdentities_=0;
  try {
   const auto bars=nativeBars();const auto primary=primaryBar();if(!primary||bars.empty()||bars.size()>32){recoveryDiagnostic(snapshot,"capture-primary-or-capacity");return false;}
   // The bitmask remains pre-Aether across Explorer recreation. A restarted
   // shell can inherit AUTOHIDE set by this owner, which is not an original.
   if(initialCapture_){APPBARDATA a{sizeof(a)};a.hWnd=primary;snapshot.taskbarState=static_cast<DWORD>(SHAppBarMessage(ABM_GETSTATE,&a));}
   for(HWND h:bars){
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);const auto birth=processBirth(pid);if(!birth){recoveryDiagnostic(snapshot,"capture-process-birth");return false;}
    LONG count=load(&snapshot.count);bool known=false;LONG slot=-1;
    for(LONG i=0;i<count;++i){const auto& entry=snapshot.entries[i];if(entry.hwnd==reinterpret_cast<std::uintptr_t>(h)&&entry.pid==pid&&entry.processBirth==birth){known=true;break;}if(slot<0&&!validEntry(entry))slot=i;}
    if(known)continue;if(slot<0){if(count>=32){recoveryDiagnostic(snapshot,"capture-capacity");return false;}slot=count;}
    snapshot.entries[slot]={reinterpret_cast<std::uintptr_t>(h),pid,IsWindowVisible(h)?1L:0L,birth};
    newIdentities_|=std::uint32_t{1}<<static_cast<unsigned>(slot);
    // Publish before any visibility/appbar mutation. Fallback may consume
    // these fields only after the helper process is confirmed dead.
    if(slot==count)InterlockedExchange(&snapshot.count,count+1);else MemoryBarrier();
   }
   return true;
  }catch(...){return false;}
 }
 bool prepareHiddenState(const RecoverySnapshot& snapshot,DWORD state) noexcept override {
  Timing timing{prepareMs_};
  APPBARDATA observed{sizeof(observed)};observed.hWnd=primaryBar();if(!observed.hWnd)return false;
  // Initial acquisition already captured GETSTATE. Later reconciliation reads
  // the raw global mask once; a matching known bar only needs visibility hidden.
  const bool globalStateMatches=!initialCapture_&&SHAppBarMessage(ABM_GETSTATE,&observed)==state;
  const LONG count=load(&snapshot.count);
  for(LONG i=0;i<count;++i){
   if(load(&snapshot.revoked))return false;const auto entry=snapshot.entries[i];if(!validEntry(entry))continue;
   if(!recoveryHiddenStateRequired(initialCapture_,globalStateMatches,(newIdentities_&(std::uint32_t{1}<<static_cast<unsigned>(i)))!=0))continue;
   // GETSTATE ignores HWND. Newly published genuine identities still need their
   // first transition even when the existing monitors already report this mask.
   // GETSTATE and identity validation may have delivered queued Shell work.
   // A command4 boundary must retain lifetime/lease priority before state writes.
   if(confirmationSnapshot_&&(!confirmationAllowed()||load(&confirmationSnapshot_->revoked)
    ||recoveryLeaseExpired(confirmationSnapshot_,GetTickCount64()))){
    InterlockedExchange(&confirmationSnapshot_->revoked,1);return false;
   }
   APPBARDATA data{sizeof(data)};data.hWnd=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));data.lParam=nativeTaskbarSetterMask(state);SHAppBarMessage(ABM_SETSTATE,&data);
  }return true;
 }
 bool requestHidden(const RecoveryEntry& entry) noexcept override {
  if(confirmationSnapshot_&&(!confirmationAllowed()||load(&confirmationSnapshot_->revoked)
   ||recoveryLeaseExpired(confirmationSnapshot_,GetTickCount64()))){
   InterlockedExchange(&confirmationSnapshot_->revoked,1);return false;
  }
  if(!validEntry(entry))return true;
  auto h=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));HideTrace t{};t.tick=GetTickCount64();t.hwnd=entry.hwnd;
  t.targetThread=GetWindowThreadProcessId(h,&t.pid);t.beforeVisible=IsWindowVisible(h);t.beforeStyle=GetWindowLongPtrW(h,GWL_STYLE);
  t.attempted=t.beforeVisible!=FALSE;
  // This synchronous call is confined to the independent sole writer. If
  // Explorer blocks it, the controller retires that helper before recovery.
  // ShowWindow returns prior visibility; only observeHidden confirms success.
  if(t.attempted)t.previousVisibleReturn=ShowWindow(h,SW_HIDE);
  t.afterVisible=IsWindowVisible(h);t.afterStyle=GetWindowLongPtrW(h,GWL_STYLE);
  if(hideTraceCount_<320)hideTrace_[hideTraceCount_++]=t;return true;
 }
 RecoveryHideObservation observeNativeVisibility() noexcept override {
  Timing timing{visibilityScanMs_};
  try {
   // This is the final native observation, after the synchronous Shell query.
   // A vanished/recreated primary or incomplete-capacity scan cannot confirm.
   const auto bars=nativeBars();const auto primary=primaryBar();
   if(!primary||bars.empty()||bars.size()>32||std::find(bars.begin(),bars.end(),primary)==bars.end())return RecoveryHideObservation::Unavailable;
   if(confirmationSnapshot_){
    const auto count=load(&confirmationSnapshot_->count);
    if(count<=0||bars.size()!=static_cast<size_t>(count))return RecoveryHideObservation::Unavailable;
    for(LONG i=0;i<count;++i){const auto& entry=confirmationSnapshot_->entries[i];
     if(!validEntry(entry)||std::find(bars.begin(),bars.end(),reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd)))==bars.end())return RecoveryHideObservation::Unavailable;
    }
   }
   for(HWND h:bars)if(!nativeTaskbarWindow(h))return RecoveryHideObservation::Unavailable;
   for(HWND h:bars)if(IsWindowVisible(h))return RecoveryHideObservation::Pending;
   return nativeTaskbarWindow(primary)?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Unavailable;
  }catch(...){return RecoveryHideObservation::Unavailable;}
 }
 RecoveryHideObservation observeNativeState(DWORD state) noexcept override {
  Timing timing{stateQueryMs_};
  APPBARDATA a{sizeof(a)};a.hWnd=primaryBar();if(!a.hWnd)return RecoveryHideObservation::Unavailable;
  return SHAppBarMessage(ABM_GETSTATE,&a)==state?RecoveryHideObservation::Confirmed:RecoveryHideObservation::Pending;
 }
 RecoveryHideObservation observeHidden(DWORD state) noexcept override {return observeRecoveryHidden(*this,state);}
 bool confirmationAllowed() const noexcept override {
  return !confirmationInterrupted_&&confirmationParent_&&confirmationStop_
   &&WaitForSingleObject(confirmationParent_,0)==WAIT_TIMEOUT
   &&WaitForSingleObject(confirmationStop_,0)==WAIT_TIMEOUT;
 }
 bool confirmationEntriesValid(const RecoverySnapshot& snapshot) noexcept override {
  try {
   const LONG count=load(&snapshot.count);const auto bars=nativeBars();const HWND primary=primaryBar();
   if(count<=0||count>32||!primary||bars.size()!=static_cast<size_t>(count)
    ||std::find(bars.begin(),bars.end(),primary)==bars.end())return false;
   for(LONG i=0;i<count;++i){
    const auto& entry=snapshot.entries[i];
    if(!validEntry(entry)||std::find(bars.begin(),bars.end(),reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd)))==bars.end())return false;
   }
   return nativeTaskbarWindow(primary);
  }catch(...){return false;}
 }
 void delayConfirmation(DWORD milliseconds) noexcept override {
  if(!confirmationAllowed()){confirmationInterrupted_=true;return;}
  const auto until=GetTickCount64()+milliseconds;HANDLE waits[]{confirmationParent_,confirmationStop_};
  do {
   // Out-of-context WinEvent callbacks require this helper's message delivery.
   // Callback work only revokes identities/signals refresh, never native writes.
   MSG message{};unsigned drained=0;
   while(drained++<128&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(message.message==WM_QUIT){confirmationInterrupted_=true;return;}
    TranslateMessage(&message);DispatchMessageW(&message);
   }
   if(!confirmationAllowed()){confirmationInterrupted_=true;return;}
   const auto now=GetTickCount64();if(now>=until)return;
   const DWORD result=MsgWaitForMultipleObjectsEx(2,waits,static_cast<DWORD>(until-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
   if(result!=WAIT_TIMEOUT&&result!=WAIT_OBJECT_0+2){confirmationInterrupted_=true;return;}
  }while(true);
 }
 bool confirmHiddenCaptured(RecoverySnapshot& snapshot) noexcept override {
  confirmationSnapshot_=&snapshot;hideTraceCount_=0;
  const auto result=confirmHiddenRecovery(&snapshot,*this);
  confirmationSnapshot_=nullptr;
  if(result==RecoveryHideResult::Complete)return true;
  flushHideTrace();recoveryDiagnostic(snapshot,"boundary-hidden-confirmation-failed",0,true);return false;
 }
 bool hideCaptured(RecoverySnapshot& snapshot) noexcept override {
  confirmationSnapshot_=&snapshot;hideTraceCount_=0;const auto result=hideRecovery(&snapshot,*this);confirmationSnapshot_=nullptr;
  if(result==RecoveryHideResult::Complete)return true;
  const DWORD error=result==RecoveryHideResult::VisibilityRequestFailed?GetLastError():0;flushHideTrace();
  const char* reason=result==RecoveryHideResult::Revoked?"hide-revoked":result==RecoveryHideResult::StateUnavailable?"hide-no-primary":result==RecoveryHideResult::ObservationUnavailable?"hide-observation-unavailable":result==RecoveryHideResult::VisibilityRequestFailed?"hide-visibility-request-failed":result==RecoveryHideResult::Deadline?"hide-confirmation-deadline":"hide-unarmed-or-invalid";
  recoveryDiagnostic(snapshot,reason,error);return false;
 }
};
}
bool nativeTaskbarWindow(HWND window) noexcept {
 if(!window||!taskbarClass(window))return false;DWORD pid=0;GetWindowThreadProcessId(window,&pid);return pid&&explorerOwner(pid);
}
void restoreSnapshot(RecoverySnapshot* snapshot,NativeRecoveryContext context) noexcept {
 if(!recoverySnapshotValid(snapshot))return;NativeRecoveryBackend backend(context);restoreRecovery(snapshot,backend);
}
bool runNativeRecoveryCommand(RecoverySnapshot* snapshot,RecoveryCommand command,ULONGLONG now,HANDLE parent,HANDLE stop) noexcept {
 if(!snapshot||snapshot->nativeAllowed!=1)return false;
 NativeRecoveryBackend backend(NativeRecoveryContext::ActiveHelper,parent,stop);
 // Optional success I/O must not delay the critical acknowledgment publication.
 // The watchdog records command/result diagnostics only after publishing ACK.
 return executeRecoveryCommand(snapshot,backend,command,now);
}
void showNativeTaskbars() noexcept{try {for(HWND h:nativeBars())ShowWindowAsync(h,SW_SHOWNA);}catch(...){}}
bool TaskbarGuard::enabled() const {return snapshot_&&load(&snapshot_->armed)!=0&&load(&snapshot_->revoked)==0&&load(&snapshot_->phase)==static_cast<LONG>(RecoveryPhase::Armed);}
bool TaskbarGuard::helperResponsive() const noexcept {
 if(!snapshot_)return false;const auto value=ticks(&snapshot_->helperHeartbeat),now=GetTickCount64();return value&&now>=value&&now-value<snapshot_->leaseMilliseconds;
}
bool TaskbarGuard::healthy() const {
 return recoveryReady(helper_&&WaitForSingleObject(helper_.value,0)==WAIT_TIMEOUT,ready_&&WaitForSingleObject(ready_.value,0)==WAIT_OBJECT_0)&&helperResponsive()&&snapshot_&&load(&snapshot_->revoked)==0&&!recoveryLeaseExpired(snapshot_,GetTickCount64());
}
DWORD TaskbarGuard::helperProcessId() const noexcept{return helper_?GetProcessId(helper_.value):0;}
bool TaskbarGuard::recoveryOwnerAlive() const noexcept{return helper_&&WaitForSingleObject(helper_.value,0)!=WAIT_OBJECT_0;}
TaskbarGuardDiagnostics TaskbarGuard::diagnostics() const noexcept {
 TaskbarGuardDiagnostics result;if(!snapshot_)return result;
 result.armed=load(&snapshot_->armed);result.count=load(&snapshot_->count);result.restored=load(&snapshot_->restored);result.revoked=load(&snapshot_->revoked);result.phase=static_cast<RecoveryPhase>(load(&snapshot_->phase));
 result.originalTaskbarState=snapshot_->taskbarState;result.leaseMilliseconds=snapshot_->leaseMilliseconds;result.heartbeat=ticks(&snapshot_->heartbeat);result.helperHeartbeat=ticks(&snapshot_->helperHeartbeat);return result;
}
bool TaskbarGuard::restorationOwned() const noexcept {
 return snapshot_&&load(&snapshot_->armed)!=0&&load(&snapshot_->revoked)!=0
  &&load(&snapshot_->phase)==static_cast<LONG>(RecoveryPhase::Restoring)
  &&helper_&&WaitForSingleObject(helper_.value,0)==WAIT_TIMEOUT
  &&ready_&&WaitForSingleObject(ready_.value,0)==WAIT_OBJECT_0
  &&stop_&&WaitForSingleObject(stop_.value,0)==WAIT_OBJECT_0&&helperResponsive();
}
bool TaskbarGuard::shutdownHelper() noexcept {
 if(snapshot_)InterlockedExchange(&snapshot_->revoked,1);
 if(stop_&&!SetEvent(stop_.value)){log("Could not request taskbar recovery shutdown.");return false;}
 // An acknowledged independent restore-only owner can be waiting for Explorer.
 // Never turn normal teardown retries into repeated kill/relaunch cycles.
 if(restorationOwned())return false;
 auto retire=[this]() noexcept {
  if(!helper_||WaitForSingleObject(helper_.value,0)==WAIT_OBJECT_0)return true;
  if(!TerminateProcess(helper_.value,9)||WaitForSingleObject(helper_.value,1000)!=WAIT_OBJECT_0){log("Recovery helper could not relinquish native ownership; retaining recovery handles.");return false;}
  log("Unresponsive owned recovery helper stopped; native ownership transferred after confirmed exit.");return true;
 };
 if(helper_&&WaitForSingleObject(helper_.value,0)==WAIT_TIMEOUT){
  if(!stop_||!SetEvent(stop_.value)){log("Could not request taskbar recovery shutdown.");return false;}
  if(WaitForSingleObject(helper_.value,2000)!=WAIT_OBJECT_0){if(restorationOwned())return false;if(!retire())return false;}
 }
 if(helper_&&WaitForSingleObject(helper_.value,0)!=WAIT_OBJECT_0)return false;
 if(snapshot_&&load(&snapshot_->armed)!=0){
  // Start an independent restore-only writer first. Its revoked mapping and
  // signalled stop event forbid hides, and keep Shell calls off the UI thread.
  bool accepted=false;try {accepted=startHelper();}catch(...){}
  if(accepted){
   if(WaitForSingleObject(helper_.value,2000)!=WAIT_OBJECT_0){
    if(restorationOwned()){log("Taskbar restoration remains pending in an acknowledged independent helper.");return false;}
    if(!retire())return false;
   }
  }else if(!retire())return false;
  // A failed or unacknowledged replacement is retired and confirmed dead
  // before this last-resort controller fallback accesses native state.
  if(helper_&&WaitForSingleObject(helper_.value,0)!=WAIT_OBJECT_0)return false;
  restoreSnapshot(snapshot_);
  if(load(&snapshot_->armed)!=0){log("Taskbar restoration remains pending; retaining controller recovery ownership.");return false;}
 }
 if(snapshot_){UnmapViewOfFile(snapshot_);snapshot_=nullptr;}
 helper_.reset();stop_.reset();parent_.reset();ready_.reset();mapping_.reset();command_.reset();completed_.reset();
 retirementStage_=RetirementStage::None;retirementDeadline_=0;nextRecoveryAttempt_=0;return true;
}
bool TaskbarGuard::startHelper(bool wait){
 auto exe=executablePath().parent_path()/L"AetherWatchdog.exe";std::error_code ec;if(!std::filesystem::is_regular_file(exe,ec)){log("Recovery helper missing. Refusing to hide taskbar.");return false;}
 if(!mapping_||!ready_||!stop_||!parent_||!command_||!completed_||!snapshot_)return false;
 ResetEvent(ready_.value);InterlockedExchange64(&snapshot_->helperHeartbeat,0);
 SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);if(!bytes)return false;
 auto* attrs=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes));if(!attrs)return false;
 if(!InitializeProcThreadAttributeList(attrs,1,0,&bytes)){HeapFree(GetProcessHeap(),0,attrs);return false;}
 HANDLE handles[]{mapping_.value,ready_.value,parent_.value,stop_.value,command_.value,completed_.value};
 bool attrOk=UpdateProcThreadAttribute(attrs,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr)!=FALSE;
 STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.lpAttributeList=attrs;PROCESS_INFORMATION pi{};
 auto value=[](HANDLE h){return std::to_wstring(reinterpret_cast<std::uintptr_t>(h));};
 std::wstring command=L"\""+exe.wstring()+L"\" "+value(parent_.value)+L" "+value(mapping_.value)+L" "+value(ready_.value)+L" "+value(stop_.value)+L" "+value(command_.value)+L" "+value(completed_.value);
 BOOL parentJob=FALSE;if(!IsProcessInJob(GetCurrentProcess(),nullptr,&parentJob))attrOk=false;
 DWORD flags=EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|CREATE_SUSPENDED;if(parentJob)flags|=CREATE_BREAKAWAY_FROM_JOB;
 BOOL created=attrOk&&CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,flags,nullptr,nullptr,&si.StartupInfo,&pi);
 DeleteProcThreadAttributeList(attrs);HeapFree(GetProcessHeap(),0,attrs);
 if(!created){log("Recovery helper process failed to start.");return false;}
 // Hold every created child, including a rejected suspended child, until its
 // death is confirmed. Async admission must not lose an unobservable writer.
 helper_.reset(pi.hProcess);BOOL helperJob=TRUE;
 if(!IsProcessInJob(helper_.value,nullptr,&helperJob)||helperJob||ResumeThread(pi.hThread)==static_cast<DWORD>(-1)){
  CloseHandle(pi.hThread);TerminateProcess(helper_.value,8);
  if(wait)WaitForSingleObject(helper_.value,1000);
  log("Recovery helper cannot obtain an independent lifetime; retaining its process handle until confirmed exit.");return false;
 }
 CloseHandle(pi.hThread);
 if(!wait)return WaitForSingleObject(helper_.value,0)==WAIT_TIMEOUT;
 HANDLE waits[]{ready_.value,helper_.value};
 if(WaitForMultipleObjects(2,waits,FALSE,2000)!=WAIT_OBJECT_0){log("Recovery helper did not acknowledge startup; no hiding requested.");return false;}
 return WaitForSingleObject(helper_.value,0)==WAIT_TIMEOUT;
}
bool TaskbarGuard::launch(){
 if(snapshot_)return healthy();
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
 mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&sa,PAGE_READWRITE,0,sizeof(RecoverySnapshot),nullptr));
 ready_.reset(CreateEventW(&sa,TRUE,FALSE,nullptr));stop_.reset(CreateEventW(&sa,TRUE,FALSE,nullptr));command_.reset(CreateEventW(&sa,FALSE,FALSE,nullptr));completed_.reset(CreateEventW(&sa,FALSE,FALSE,nullptr));parent_.reset(OpenProcess(SYNCHRONIZE,TRUE,GetCurrentProcessId()));
 if(!mapping_||!ready_||!stop_||!command_||!completed_||!parent_){shutdownHelper();return false;}
 snapshot_=static_cast<RecoverySnapshot*>(MapViewOfFile(mapping_.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(RecoverySnapshot)));if(!snapshot_){shutdownHelper();return false;}
 new(snapshot_) RecoverySnapshot{};snapshot_->nativeAllowed=1;snapshot_->leaseMilliseconds=std::clamp<DWORD>(options_.leaseMilliseconds,1000,60000);InterlockedExchange64(&snapshot_->heartbeat,static_cast<LONG64>(GetTickCount64()));
 if(!startHelper()){shutdownHelper();return false;}return true;
}
bool TaskbarGuard::request(RecoveryCommand command,bool wait,DWORD timeoutMilliseconds) noexcept {
 if(!snapshot_||!command_||!completed_||!helper_||WaitForSingleObject(helper_.value,0)!=WAIT_TIMEOUT)return false;
 if(wait)ResetEvent(completed_.value);
 InterlockedExchange(&snapshot_->command,static_cast<LONG>(command));const LONG sequence=InterlockedIncrement(&snapshot_->commandSequence);
 if(!SetEvent(command_.value))return false;if(!wait)return true;
 const auto until=GetTickCount64()+timeoutMilliseconds;HANDLE waits[]{completed_.value,helper_.value};
 for(;;){
  if(load(&snapshot_->acknowledgedSequence)==sequence)return load(&snapshot_->commandSucceeded)!=0;
  const auto now=GetTickCount64();if(now>=until)return false;
  if(WaitForMultipleObjects(2,waits,FALSE,static_cast<DWORD>(until-now))!=WAIT_OBJECT_0)return false;
 }
}
bool TaskbarGuard::disableAsync() noexcept {
 revoked_=true;if(snapshot_)InterlockedExchange(&snapshot_->revoked,1);
 if(stop_&&!SetEvent(stop_.value)){log("Could not request asynchronous taskbar recovery; retaining ownership.");return false;}
 const auto now=GetTickCount64();
 if(helper_){
  const DWORD lifetime=WaitForSingleObject(helper_.value,0);
  if(lifetime!=WAIT_OBJECT_0){
   // An unobservable held process cannot grant local native ownership.
   if(lifetime!=WAIT_TIMEOUT)return false;
   auto terminateOwned=[&]() noexcept {
    TerminateProcess(helper_.value,9);
    retirementStage_=RetirementStage::Terminating;retirementDeadline_=now+1000;
   };
   if(retirementStage_==RetirementStage::Terminating){
    if(now>=retirementDeadline_)terminateOwned();
    return false;
   }
   if(retirementStage_==RetirementStage::Starting){
    const bool acknowledged=ready_&&WaitForSingleObject(ready_.value,0)==WAIT_OBJECT_0;
    if(!acknowledged){if(now>=retirementDeadline_)terminateOwned();return false;}
    retirementStage_=RetirementStage::Stopping;retirementDeadline_=now+2000;
   }
   // A ready, responsive independent Restoring owner may legitimately wait
   // for Explorer/topology. Never kill/relaunch it merely because it is pending.
   if(restorationOwned()){retirementStage_=RetirementStage::Restoring;retirementDeadline_=0;return false;}
   if(retirementStage_==RetirementStage::None||retirementStage_==RetirementStage::Restoring){
    retirementStage_=RetirementStage::Stopping;retirementDeadline_=now+2000;
   }
   if(now>=retirementDeadline_)terminateOwned();
   return false;
  }
  helper_.reset();retirementStage_=RetirementStage::None;retirementDeadline_=0;
 }
 if(snapshot_&&load(&snapshot_->armed)!=0){
  // No native fallback is allowed on this UI path. Failed launch retains the
  // immutable parent context and retries with backoff; terminal teardown uses
  // the existing synchronous last-owner recovery outside the message loop.
  if(now<nextRecoveryAttempt_)return false;
  nextRecoveryAttempt_=now+1000;
  bool launched=false;try{launched=startHelper(false);}catch(...){}
  if(helper_){
   retirementStage_=launched?RetirementStage::Starting:RetirementStage::Terminating;
   retirementDeadline_=GetTickCount64()+(launched?2000:1000);
  }
  return false;
 }
 if(snapshot_){UnmapViewOfFile(snapshot_);snapshot_=nullptr;}
 helper_.reset();stop_.reset();parent_.reset();ready_.reset();mapping_.reset();command_.reset();completed_.reset();
 retirementStage_=RetirementStage::None;retirementDeadline_=0;nextRecoveryAttempt_=0;revoked_=false;return true;
}
void TaskbarGuard::failClosed(bool asynchronous) noexcept {
 revoked_=true;if(snapshot_)InterlockedExchange(&snapshot_->revoked,1);
 if(asynchronous)disableAsync();else shutdownHelper();
}
bool TaskbarGuard::setEnabled(bool active){
 if(!active){const bool result=shutdownHelper();if(result)revoked_=false;return result;}
 if constexpr (!TaskbarReplacementValidated){if(!options_.enableNativeForValidation){log("Taskbar replacement remains unavailable pending native recovery acceptance.");return false;}}
 if(revoked_)return false;
 if(snapshot_){if(!healthy()){recoveryDiagnostic(*snapshot_,"guard-existing-owner-unhealthy",0,false);failClosed();return false;}if(enabled())return true;}
 const auto began=GetTickCount64();const bool launched=launch();
 // Two physical taskbars can consume just over two seconds in synchronous
 // Shell state changes. Allow that initial transaction a bounded 3.5 seconds;
 // the independent five-second lease, Stop/parent checks and exact native
 // confirmation still control admission. Refresh keeps its shorter deadline.
 const bool acknowledged=launched&&request(RecoveryCommand::Arm,true,3500);
 const bool hideEnabled=acknowledged&&enabled();const bool good=hideEnabled&&healthy();
 // This is a live controller renewal after healthy ACK, not a helper heartbeat.
 // Its UI timer begins after admission returns; optional logging must not inherit
 // the old prelaunch lease. A full fresh-lease stall still fails closed.
 if(good)InterlockedExchange64(&snapshot_->heartbeat,static_cast<LONG64>(GetTickCount64()));
 try {std::ostringstream out;out<<"Taskbar guard Arm DIAG process="<<GetCurrentProcessId()<<" helper="<<helperProcessId()<<" launched="<<launched<<" acknowledged="<<acknowledged<<" enabled="<<hideEnabled<<" healthy="<<good<<" elapsedMs="<<(GetTickCount64()-began);log(out.str());}catch(...){}
 if(!good){if(snapshot_)recoveryDiagnostic(*snapshot_,"guard-arm-admission-failed",0,false);failClosed();return false;}return true;
}
void TaskbarGuard::refresh(bool asynchronousFailure){
 if(!snapshot_||revoked_)return;
 if(!healthy()){recoveryDiagnostic(*snapshot_,"guard-refresh-owner-unhealthy",0,false);failClosed(asynchronousFailure);log("Taskbar replacement revoked; original native desktop restoration requested.");return;}
 if(enabled()&&!request(RecoveryCommand::Refresh,false))failClosed(asynchronousFailure);
}
bool TaskbarGuard::refreshAndConfirm() noexcept {
 if(!enabled()||!healthy()){
  if(snapshot_)recoveryDiagnostic(*snapshot_,"guard-confirm-existing-owner-unhealthy",0,false);
  failClosed(true);return false;
 }
  // Renew the controller lease before this explicit, bounded UI boundary wait.
 // Only the independent helper performs the quiet fence and native observations.
 InterlockedExchange64(&snapshot_->heartbeat,static_cast<LONG64>(GetTickCount64()));
 const bool acknowledged=request(RecoveryCommand::ConfirmHidden,true,3000);
 const bool confirmed=acknowledged&&enabled()&&healthy();
 if(!confirmed){
  if(snapshot_)recoveryDiagnostic(*snapshot_,"guard-refresh-confirmation-failed",0,false);
  failClosed(true);
 }
 return confirmed;
}
void TaskbarGuard::heartbeat(bool asynchronousFailure) noexcept {
 if(!snapshot_||revoked_)return;if(!healthy()){recoveryDiagnostic(*snapshot_,"guard-heartbeat-owner-unhealthy",0,false);failClosed(asynchronousFailure);return;}
 if(enabled())InterlockedExchange64(&snapshot_->heartbeat,static_cast<LONG64>(GetTickCount64()));
}
TaskbarGuard::~TaskbarGuard(){
 bool reported=false;DWORD wait=1000;
 for(;;){
  // During normal teardown this succeeds immediately. If Explorer is absent
  // AND no recovery child can start, this already-hidden, provider-free
  // process remains the last restoration owner instead of dropping originals.
  if(restorationOwned()||shutdownHelper())break;
  if(restorationOwned())break;
  if(!reported){log("Retaining passive controller process until native restoration or independent ownership transfer is confirmed.");reported=true;}
  Sleep(wait);wait=std::min<DWORD>(wait+1000,5000);
 }
 if(snapshot_)UnmapViewOfFile(snapshot_);
}
}
