#include "platform/safety.hpp"
#include "platform/recovery_engine.hpp"
#include "platform/config.hpp"
#include "platform/extensions.hpp"
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <oleacc.h>
#include <iostream>
#include <functional>
#include <limits>
#include <cerrno>
#include <new>
#include <array>
#include <sstream>
// Opt-in only. A detached sentinel acknowledges outside the supervisor's job
// before a suspended fixture runs. It restores an immutable native before-state
// on supervisor death/deadline after stopping only recorded owned processes.
namespace {
using namespace aether;using namespace aether::win;
constexpr DWORD StateMagic=0x4E545238,StateVersion=2;
constexpr UINT ArmCommand=WM_APP+71,RestoreCommand=WM_APP+72,BlockCommand=WM_APP+73,
 ProbeCommand=WM_APP+74,EnableCommand=WM_APP+75,CloseCommand=WM_APP+76,ArmUnpublishedCommand=WM_APP+77,HeartbeatTimer=1;
constexpr LONG PhaseReady=1,PhaseArmed=2,PhaseBlocked=3,PhaseResumed=4,PhaseArming=5,PhaseArmUnpublished=6;
// Native admission uses the production lease; Shell transactions can exceed two seconds.
constexpr DWORD LeaseMilliseconds=TaskbarGuardOptions{}.leaseMilliseconds,SentinelLifetime=120000;
struct BarBefore {std::uint64_t hwnd{};DWORD pid{};ULONGLONG birth{};LONG visible{},primary{};};
struct MonitorBefore {wchar_t device[CCHDEVICENAME]{};RECT bounds{},work{};};
struct VisualBefore {DWORD exStyle{},key{},flags{};BYTE alpha=255;};
struct AcceptanceState {
 DWORD magic=StateMagic,version=StateVersion,bytes=sizeof(AcceptanceState);
 DWORD barCount{},monitorCount{},taskbarState{};
 BarBefore bars[32]{};MonitorBefore monitors[32]{};
 volatile LONG fixturePid=0,fixtureKind=0,helperPid=0,fixturePhase=0;
 alignas(8) volatile LONG64 fixtureBirth=0,helperBirth=0,fixtureWindow=0;
 volatile LONG lastCommand=-1,sentinelTriggered=0,sentinelRestored=0,fixtureAccepted=0,sentinelDiscoveries=0;
 DWORD supervisorPid{};ULONGLONG supervisorBirth{},deadline{};
 VisualBefore visual[32]{};
};
struct Child {
 Handle process,thread;DWORD pid{};ULONGLONG birth{};
 bool alive()const{return process&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT;}
};
ULONGLONG birthOf(HANDLE process){
 FILETIME born{},ended{},kernel{},user{};
 return process&&GetProcessTimes(process,&born,&ended,&kernel,&user)
  ?(static_cast<ULONGLONG>(born.dwHighDateTime)<<32)|born.dwLowDateTime:0;
}
ULONGLONG birthOf(DWORD pid){Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));return process?birthOf(process.value):0;}
std::wstring imageOf(HANDLE process){
 std::array<wchar_t,32768> image{};DWORD length=static_cast<DWORD>(image.size());
 return process&&QueryFullProcessImageNameW(process,0,image.data(),&length)?std::wstring(image.data(),length):std::wstring{};
}
std::wstring explorerImage(){
 std::array<wchar_t,32768> directory{};auto length=GetWindowsDirectoryW(directory.data(),static_cast<UINT>(directory.size()));
 return length&&length<directory.size()?(std::filesystem::path(directory.data())/L"explorer.exe").native():std::wstring{};
}
bool taskbarClass(HWND window,bool* primary=nullptr){
 wchar_t name[64]{};if(!GetClassNameW(window,name,64))return false;
 const bool first=wcscmp(name,L"Shell_TrayWnd")==0;
 if(primary)*primary=first;return first||wcscmp(name,L"Shell_SecondaryTrayWnd")==0;
}
bool genuineBar(HWND window,DWORD* pid=nullptr,ULONGLONG* birth=nullptr){
 if(!taskbarClass(window))return false;DWORD owner=0;GetWindowThreadProcessId(window,&owner);
 Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,owner));
 const auto expected=explorerImage(),actual=process?imageOf(process.value):std::wstring{};
 const auto born=process?birthOf(process.value):0;
 if(!process||!born||WaitForSingleObject(process.value,0)!=WAIT_TIMEOUT||expected.empty()
  ||_wcsicmp(expected.c_str(),actual.c_str())!=0)return false;
 if(pid)*pid=owner;if(birth)*birth=born;return true;
}
bool sameBar(const BarBefore& entry){
 HWND window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(entry.hwnd));DWORD pid=0;ULONGLONG born=0;
 return genuineBar(window,&pid,&born)&&pid==entry.pid&&born==entry.birth;
}
bool validState(const AcceptanceState* state){
 if(!state||state->magic!=StateMagic||state->version!=StateVersion||state->bytes!=sizeof(AcceptanceState)
  ||state->barCount<1||state->barCount>32||state->monitorCount<1||state->monitorCount>32
  ||!state->supervisorPid||!state->supervisorBirth)return false;
 unsigned primary=0;
 for(DWORD i=0;i<state->barCount;++i){
  const auto& bar=state->bars[i];
  if(!bar.hwnd||!bar.pid||!bar.birth||(bar.visible!=0&&bar.visible!=1)||(bar.primary!=0&&bar.primary!=1))return false;
  if(bar.primary)++primary;
  for(DWORD j=0;j<i;++j)if(state->bars[j].hwnd==bar.hwnd)return false;
 }
 return primary==1;
}
void preflightDiagnostic(const char* stage,DWORD error){
 std::cerr<<"DIAG native preflight: "<<stage<<"; process="<<GetCurrentProcessId()<<"; Win32Error="<<error<<std::endl;
}
void describeNativeBars(){
 const auto expected=explorerImage();POINT pointer{};const bool pointerObserved=GetCursorPos(&pointer)!=FALSE;
 std::wcerr<<L"DIAG native preflight: expected Explorer image="<<expected
  <<L"; pointerObserved="<<pointerObserved<<L"; pointer=("<<pointer.x<<L","<<pointer.y<<L")"<<std::endl;
 struct CursorObservation{POINT pointer;bool observed;};CursorObservation cursor{pointer,pointerObserved};
 EnumWindows([](HWND window,LPARAM value)->BOOL{
  bool primary=false;if(!taskbarClass(window,&primary))return TRUE;
  DWORD pid=0;GetWindowThreadProcessId(window,&pid);const auto& cursor=*reinterpret_cast<CursorObservation*>(value);
  RECT bounds{};const bool boundsObserved=GetWindowRect(window,&bounds)!=FALSE;
  Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));const auto error=process?ERROR_SUCCESS:GetLastError();
  std::wcerr<<L"DIAG native bar: hwnd="<<reinterpret_cast<std::uintptr_t>(window)
   <<L"; primary="<<primary<<L"; pid="<<pid<<L"; birth="<<(process?birthOf(process.value):0)
   <<L"; visible="<<(IsWindowVisible(window)!=FALSE)<<L"; image="<<(process?imageOf(process.value):std::wstring{})
   <<L"; OpenProcessError="<<error<<L"; boundsObserved="<<boundsObserved
   <<L"; rect=("<<bounds.left<<L","<<bounds.top<<L","<<bounds.right<<L","<<bounds.bottom<<L")"
   <<L"; pointerInside="<<(cursor.observed&&boundsObserved&&PtInRect(&bounds,cursor.pointer))<<std::endl;return TRUE;
 },reinterpret_cast<LPARAM>(&cursor));
}std::vector<HWND> independentlyEnumeratedBars(){std::vector<HWND> bars;EnumWindows([](HWND h,LPARAM p)->BOOL{if(taskbarClass(h))reinterpret_cast<std::vector<HWND>*>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&bars));for(const auto* name:{L"Shell_TrayWnd",L"Shell_SecondaryTrayWnd"}){HWND after=nullptr;for(int i=0;i<33;++i){auto h=FindWindowExW(nullptr,after,name,nullptr);if(!h)break;after=h;if(std::find(bars.begin(),bars.end(),h)==bars.end())bars.push_back(h);}}return bars;}
bool captureBefore(AcceptanceState& state){
 struct Enumeration{AcceptanceState* state;bool overflow=false,untrusted=false;};Enumeration enumeration{&state};
 for(auto window:independentlyEnumeratedBars()){bool primary=false;if(!taskbarClass(window,&primary))continue;DWORD pid=0;ULONGLONG born=0;if(!genuineBar(window,&pid,&born)){enumeration.untrusted=true;continue;}if(state.barCount==32){enumeration.overflow=true;break;}const auto index=state.barCount++;state.bars[index]={reinterpret_cast<std::uintptr_t>(window),pid,born,IsWindowVisible(window)?1L:0L,primary?1L:0L};
  auto& visual=state.visual[index];visual.exStyle=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
  if((visual.exStyle&WS_EX_LAYERED)&&!GetLayeredWindowAttributes(window,&visual.key,&visual.alpha,&visual.flags))return false;}
 struct MonitorEnumeration{AcceptanceState* state;bool failed=false;};MonitorEnumeration monitors{&state};
 EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM value)->BOOL{
  auto& enumeration=*reinterpret_cast<MonitorEnumeration*>(value);MONITORINFOEXW info{};info.cbSize=sizeof(info);
  if(!GetMonitorInfoW(monitor,&info)||enumeration.state->monitorCount==32){enumeration.failed=true;return FALSE;}
  auto& before=enumeration.state->monitors[enumeration.state->monitorCount++];
  wcscpy_s(before.device,info.szDevice);before.bounds=info.rcMonitor;before.work=info.rcWork;return TRUE;
 },reinterpret_cast<LPARAM>(&monitors));
 state.supervisorPid=GetCurrentProcessId();state.supervisorBirth=birthOf(GetCurrentProcess());state.deadline=GetTickCount64()+SentinelLifetime;
 if(enumeration.overflow||enumeration.untrusted||monitors.failed||!validState(&state)){
  std::cerr<<"DIAG native capture rejected: bars="<<state.barCount<<"; monitors="<<state.monitorCount
   <<"; overflow="<<enumeration.overflow<<"; untrusted="<<enumeration.untrusted
   <<"; monitorFailure="<<monitors.failed<<"; validState="<<validState(&state)<<std::endl;
  describeNativeBars();return false;
 }
 HWND primary=nullptr;bool observable=false;
 for(DWORD i=0;i<state.barCount;++i){if(state.bars[i].primary)primary=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd));observable=observable||state.bars[i].visible!=0;}
 if(!primary||!observable){std::cerr<<"DIAG native capture rejected: primary="<<(primary!=nullptr)<<"; anyVisible="<<observable<<std::endl;describeNativeBars();return false;}
 APPBARDATA data{sizeof(data)};data.hWnd=primary;state.taskbarState=static_cast<DWORD>(SHAppBarMessage(ABM_GETSTATE,&data));return true;
}
HWND originalPrimary(const AcceptanceState& state){
 for(DWORD i=0;i<state.barCount;++i)if(state.bars[i].primary&&sameBar(state.bars[i]))return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd));
 return nullptr;
}
bool barSetUnchanged(const AcceptanceState& state){
 const auto bars=independentlyEnumeratedBars();if(bars.size()!=state.barCount)return false;for(auto h:bars){bool found=false;for(DWORD i=0;i<state.barCount;++i)if(state.bars[i].hwnd==reinterpret_cast<std::uintptr_t>(h)&&sameBar(state.bars[i]))found=true;if(!found)return false;}return true;
}
bool presentationRestored(const AcceptanceState& state){
 for(DWORD i=0;i<state.barCount;++i){
  if(!sameBar(state.bars[i]))return false;auto h=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd));const auto& v=state.visual[i];
  if((GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_LAYERED)!=(v.exStyle&WS_EX_LAYERED))return false;
  if(v.exStyle&WS_EX_LAYERED){COLORREF key=0;BYTE alpha=0;DWORD flags=0;if(!GetLayeredWindowAttributes(h,&key,&alpha,&flags)||flags!=v.flags||((flags&LWA_ALPHA)&&alpha!=v.alpha)||((flags&LWA_COLORKEY)&&key!=v.key))return false;}
 }return true;
}
void restoreRecordedPresentation(const AcceptanceState& state,HWND h){
 for(DWORD i=0;i<state.barCount;++i)if(state.bars[i].hwnd==reinterpret_cast<std::uintptr_t>(h)&&sameBar(state.bars[i])){
  const auto& v=state.visual[i];const auto style=GetWindowLongPtrW(h,GWL_EXSTYLE);
  if(v.exStyle&WS_EX_LAYERED){if(!(style&WS_EX_LAYERED))SetWindowLongPtrW(h,GWL_EXSTYLE,style|WS_EX_LAYERED);SetLayeredWindowAttributes(h,v.key,v.alpha,v.flags);}
  else if(style&WS_EX_LAYERED)SetWindowLongPtrW(h,GWL_EXSTYLE,style&~static_cast<LONG_PTR>(WS_EX_LAYERED));return;
 }
}
bool barsRestored(const AcceptanceState& state){
 if(!presentationRestored(state))return false;
 if(!validState(&state))return false;const auto primary=originalPrimary(state);if(!primary)return false;
 APPBARDATA data{sizeof(data)};data.hWnd=primary;if(SHAppBarMessage(ABM_GETSTATE,&data)!=state.taskbarState)return false;
 // Shell queries can process pending visibility changes. Validate the exact
 // original identity set and visibility only after the synchronous query.
 if(!originalPrimary(state)||!barSetUnchanged(state))return false;
 for(DWORD i=0;i<state.barCount;++i)if(!sameBar(state.bars[i])
  ||(IsWindowVisible(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd)))!=FALSE)!=(state.bars[i].visible!=0))return false;
 return true;
}
bool barsHidden(const AcceptanceState& state,bool requireInvisible=true){
 if(!validState(&state))return false;const auto primary=originalPrimary(state);if(!primary)return false;
 APPBARDATA data{sizeof(data)};data.hWnd=primary;if(SHAppBarMessage(ABM_GETSTATE,&data)!=(state.taskbarState|ABS_AUTOHIDE))return false;
 if(!originalPrimary(state)||!barSetUnchanged(state))return false;
 for(DWORD i=0;i<state.barCount;++i)if(!sameBar(state.bars[i])
  ||(requireInvisible&&IsWindowVisible(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd)))))return false;
 for(DWORD i=0;i<state.barCount;++i){auto h=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd));COLORREF key=0;BYTE alpha=255;DWORD flags=0;
  if(!(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_LAYERED)||!GetLayeredWindowAttributes(h,&key,&alpha,&flags)||!(flags&LWA_ALPHA)||alpha!=0)return false;}
 return true;
}bool workAreasRestored(const AcceptanceState& state,bool includeWork=true){
 struct Enumeration{const AcceptanceState* state;bool includeWork;DWORD count=0;bool same=true;};Enumeration enumeration{&state,includeWork};
 const bool enumerated=EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM value)->BOOL{
  auto& enumeration=*reinterpret_cast<Enumeration*>(value);MONITORINFOEXW info{};info.cbSize=sizeof(info);++enumeration.count;
  if(!GetMonitorInfoW(monitor,&info)){enumeration.same=false;return TRUE;}bool found=false;
  for(DWORD i=0;i<enumeration.state->monitorCount;++i){const auto& before=enumeration.state->monitors[i];
   if(wcscmp(before.device,info.szDevice)==0){found=EqualRect(&before.bounds,&info.rcMonitor)&&(!enumeration.includeWork||EqualRect(&before.work,&info.rcWork));break;}}
  if(!found)enumeration.same=false;return TRUE;
 },reinterpret_cast<LPARAM>(&enumeration))!=FALSE;
 return enumerated&&enumeration.same&&enumeration.count==state.monitorCount;
}void describeWorkAreas(const AcceptanceState& state,const char* stage){
 std::cerr<<"DIAG physical work areas: stage="<<stage<<"; tick="<<GetTickCount64()<<"; capturedMonitors="<<state.monitorCount<<std::endl;
 for(DWORD i=0;i<state.monitorCount;++i){const auto& m=state.monitors[i];
  std::wcerr<<L"DIAG captured monitor: device="<<m.device<<L"; bounds=("<<m.bounds.left<<L","<<m.bounds.top<<L","<<m.bounds.right<<L","<<m.bounds.bottom
   <<L"); work=("<<m.work.left<<L","<<m.work.top<<L","<<m.work.right<<L","<<m.work.bottom<<L")"<<std::endl;
 }
 EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM)->BOOL{
  MONITORINFOEXW m{};m.cbSize=sizeof(m);
  if(GetMonitorInfoW(monitor,&m))std::wcerr<<L"DIAG current monitor: device="<<m.szDevice<<L"; bounds=("<<m.rcMonitor.left<<L","<<m.rcMonitor.top<<L","<<m.rcMonitor.right<<L","<<m.rcMonitor.bottom
   <<L"); work=("<<m.rcWork.left<<L","<<m.rcWork.top<<L","<<m.rcWork.right<<L","<<m.rcWork.bottom<<L")"<<std::endl;
  else std::cerr<<"DIAG current monitor unavailable; Win32Error="<<GetLastError()<<std::endl;
  return TRUE;
 },0);
}
bool waitFor(const std::function<bool()>& condition,DWORD timeout){
 const auto until=GetTickCount64()+timeout;do{if(condition())return true;Sleep(20);}while(GetTickCount64()<until);return condition();
}
HWND currentPrimary(const AcceptanceState& state){
 if(auto primary=originalPrimary(state))return primary;
 HWND result=nullptr;
 EnumWindows([](HWND window,LPARAM value)->BOOL{
  bool primary=false;if(taskbarClass(window,&primary)&&primary&&genuineBar(window)){*reinterpret_cast<HWND*>(value)=window;return FALSE;}return TRUE;
 },reinterpret_cast<LPARAM>(&result));return result;
}
bool recoveryRestored(const AcceptanceState& state){
 if(!presentationRestored(state))return false;
 HWND primary=currentPrimary(state);if(!primary)return false;
 APPBARDATA data{sizeof(data)};data.hWnd=primary;if(SHAppBarMessage(ABM_GETSTATE,&data)!=state.taskbarState)return false;
 if(!currentPrimary(state))return false;
 for(DWORD i=0;i<state.barCount;++i)if(sameBar(state.bars[i])
  &&(IsWindowVisible(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(state.bars[i].hwnd)))!=FALSE)!=(state.bars[i].visible!=0))return false;
 struct Enumeration{const AcceptanceState* state;bool visible=true;};Enumeration enumeration{&state};
 EnumWindows([](HWND window,LPARAM value)->BOOL{
  auto& enumeration=*reinterpret_cast<Enumeration*>(value);if(!genuineBar(window))return TRUE;
  bool original=false;for(DWORD i=0;i<enumeration.state->barCount;++i)if(enumeration.state->bars[i].hwnd==reinterpret_cast<std::uintptr_t>(window)&&sameBar(enumeration.state->bars[i]))original=true;
  if(!original&&!IsWindowVisible(window))enumeration.visible=false;return TRUE;
 },reinterpret_cast<LPARAM>(&enumeration));
 // Exact HWND/monitor before-state remains an acceptance assertion. Recovery
 // can also restore a newly recreated genuine Explorer primary and bars.
 return enumeration.visible&&workAreasRestored(state);
}
bool restoreCapturedWorkAreas(const AcceptanceState& state,AcceptanceState* observation){
 if(!workAreasRestored(state,false))return false;
 for(DWORD i=0;i<state.monitorCount;++i){
  if(!workAreasRestored(state,false))return false;
  const auto& before=state.monitors[i];RECT desired=before.work;MONITORINFOEXW current{};current.cbSize=sizeof(current);
  const auto monitor=MonitorFromRect(&desired,MONITOR_DEFAULTTONULL);
  if(!monitor||!GetMonitorInfoW(monitor,&current)||wcscmp(current.szDevice,before.device)
   ||!EqualRect(&current.rcMonitor,&before.bounds))return false;
  if(EqualRect(&current.rcWork,&before.work))continue;
  // Native writers are already retired by each directlyRestore caller.
  // Rescue remains a failed ordinary product outcome even if cleanup works.
  if(observation)InterlockedExchange(&observation->sentinelTriggered,1);
  if(!SystemParametersInfoW(SPI_SETWORKAREA,0,&desired,0))return false;
 }
 return true;
}
struct NativeBar {HWND window{};DWORD pid{};ULONGLONG birth{};};
bool sameNativeIdentity(const NativeBar& a,const NativeBar& b){return a.window==b.window&&a.pid==b.pid&&a.birth==b.birth;}
bool captureRestoreBars(std::array<NativeBar,32>& bars,DWORD& count){
 struct Enumeration{std::array<NativeBar,32>* bars;DWORD count=0;bool complete=true;};Enumeration enumeration{&bars};
 const bool complete=EnumWindows([](HWND window,LPARAM value)->BOOL{
  auto& enumeration=*reinterpret_cast<Enumeration*>(value);DWORD pid=0;ULONGLONG born=0;
  if(!genuineBar(window,&pid,&born))return TRUE;
  if(enumeration.count==enumeration.bars->size()){enumeration.complete=false;return FALSE;}
  (*enumeration.bars)[enumeration.count++]={window,pid,born};return TRUE;
 },reinterpret_cast<LPARAM>(&enumeration))!=FALSE;
 count=enumeration.count;return complete&&enumeration.complete;
}
void pruneRestoreIdentities(const std::array<NativeBar,32>& current,DWORD count,std::array<NativeBar,32>& applied,DWORD& appliedCount){
 DWORD retained=0;
 for(DWORD i=0;i<appliedCount;++i)
  if(std::any_of(current.begin(),current.begin()+count,[&](const NativeBar& bar){return sameNativeIdentity(applied[i],bar);}))
   applied[retained++]=applied[i];
 appliedCount=retained;
}
bool originalVisibleTarget(const AcceptanceState& state,const NativeBar& bar){
 for(DWORD i=0;i<state.barCount;++i){const auto& original=state.bars[i];
  if(original.hwnd==reinterpret_cast<std::uintptr_t>(bar.window)&&original.pid==bar.pid&&original.birth==bar.birth)return original.visible!=0;
 }
 return true; // newly genuine bars must be made visible during recovery
}
bool requestRecordedVisibility(const AcceptanceState& state,const std::array<NativeBar,32>& current,DWORD count,std::array<NativeBar,32>& applied,DWORD& appliedCount){
 pruneRestoreIdentities(current,count,applied,appliedCount);
 for(DWORD i=0;i<count;++i){const auto& bar=current[i];
  const bool fresh=std::none_of(applied.begin(),applied.begin()+appliedCount,[&](const NativeBar& old){return sameNativeIdentity(old,bar);});
  const bool target=originalVisibleTarget(state,bar);
  if(fresh||(IsWindowVisible(bar.window)!=FALSE)!=target){
   DWORD pid=0;ULONGLONG born=0;
   if(!genuineBar(bar.window,&pid,&born)||pid!=bar.pid||born!=bar.birth)return false;
   // This sole recovery writer runs only after all owned writers are dead.
   // ShowWindow's return is prior visibility; fresh observations prove success.
   restoreRecordedPresentation(state,bar.window);ShowWindow(bar.window,target?SW_SHOWNA:SW_HIDE);
   if(fresh)applied[appliedCount++]=bar;
  }
 }
 return true;
}
bool rescueCoverageConfirmed(const AcceptanceState& state,const std::array<NativeBar,32>& states,DWORD stateCount,const std::array<NativeBar,32>& targets,DWORD targetCount){
 std::array<NativeBar,32> current{};DWORD count=0;if(!captureRestoreBars(current,count)||!count)return false;
 for(DWORD i=0;i<count;++i){const auto& bar=current[i];
  if(std::none_of(states.begin(),states.begin()+stateCount,[&](const NativeBar& old){return sameNativeIdentity(old,bar);})
   ||std::none_of(targets.begin(),targets.begin()+targetCount,[&](const NativeBar& old){return sameNativeIdentity(old,bar);})
   ||(IsWindowVisible(bar.window)!=FALSE)!=originalVisibleTarget(state,bar))return false;
 }
 return true;
}
std::filesystem::path sentinelDiagnosticProfile(DWORD pid,ULONGLONG born){
 return executablePath().parent_path()/L"test-profiles"/(L"native-sentinel-"+std::to_wstring(pid)+L"-"+std::to_wstring(born));
}
void persistRecoveryDiagnostic(const AcceptanceState& state,const char* stage,const char* reason,ULONGLONG started=0) noexcept{
 try{
  const auto tick=GetTickCount64();std::ostringstream out;
  out<<"Native fixture/sentinel DIAG stage="<<stage<<" reason="<<reason<<" process="<<GetCurrentProcessId()<<" tick="<<tick
   <<" elapsedMs="<<(started?tick-started:0)<<" fixturePid="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&state.fixturePid),0,0)
   <<" fixturePhase="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&state.fixturePhase),0,0)
   <<" originalMask="<<state.taskbarState<<" originalMonitors="<<state.monitorCount;
  for(DWORD i=0;i<state.monitorCount;++i){const auto& m=state.monitors[i];
   out<<" [original="<<i<<" device="<<utf8(m.device)<<" bounds=("<<m.bounds.left<<","<<m.bounds.top<<","<<m.bounds.right<<","<<m.bounds.bottom
    <<") work=("<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<")]";
  }
  WorkAreaSnapshot current;const bool captured=captureWorkAreas(current);out<<" currentCapture="<<captured;
  if(captured)for(DWORD i=0;i<current.count;++i){const auto& m=current.entries[i];
   out<<" [current="<<i<<" device="<<utf8(m.device)<<" bounds=("<<m.bounds.left<<","<<m.bounds.top<<","<<m.bounds.right<<","<<m.bounds.bottom
    <<") work=("<<m.work.left<<","<<m.work.top<<","<<m.work.right<<","<<m.work.bottom<<")]";
  }
  for(DWORD i=0;i<state.barCount;++i){const auto& b=state.bars[i];const auto window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(b.hwnd));
   out<<" [bar="<<i<<" hwnd="<<b.hwnd<<" originalVisible="<<b.visible<<" identityMatches="<<sameBar(b)<<" currentVisible="<<(IsWindowVisible(window)!=FALSE)<<"]";
  }
  // No Shell query or native write is introduced by this diagnostic.
  log(out.str());
 }catch(...){}
}
bool directlyRestore(const AcceptanceState& state,AcceptanceState* observation){
 if(!validState(&state)||!observation)return false;
 // Every caller has already confirmed the fixture and all owned writers dead.
 // These fixtures use the current synchronous native hide implementation.
 // Observe an already exact desktop without any native write: even a same-
 // visible ShowWindowAsync request can discard its original custom work areas.
 const auto observationStarted=GetTickCount64();
 const bool entryNative=barsRestored(state);
 const bool entryWork=entryNative&&workAreasRestored(state);
 bool confirmedClean=entryNative&&entryWork;
 const char* dirtyReason=!entryNative?"entry-native-mismatch":!entryWork?"entry-workarea-mismatch":"none";
 if(confirmedClean){
  const auto until=GetTickCount64()+100;
  do{
   Sleep(20);const bool native=barsRestored(state);const bool work=native&&workAreasRestored(state);
   if(!native||!work){dirtyReason=!native?"quiet-native-drift":"quiet-workarea-drift";confirmedClean=false;break;}
  }
  while(GetTickCount64()<until);
 }
 if(confirmedClean){
  persistRecoveryDiagnostic(state,"sentinel-clean-path","saved-entry-and-quiet-exact",observationStarted);
  std::cerr<<"DIAG sentinel restoration: path=confirmed-natural-cleanup; nativeWrites=0."<<std::endl;
  return true;
 }
 // A dirty entry or observed drift is product failure before any rescue
 // visibility/state/geometry request; successful cleanup cannot erase that.
 InterlockedExchange(&observation->sentinelTriggered,1);
 persistRecoveryDiagnostic(state,"sentinel-dirty-path",dirtyReason,observationStarted);
 std::cerr<<"DIAG sentinel restoration: path=recovery-required; intervention recorded before rescue writes."<<std::endl;
 std::array<NativeBar,32> applied{},targetsApplied{};DWORD appliedCount=0,targetsCount=0;
 const auto deadline=GetTickCount64()+10000;
 return waitFor([&]{
  HWND primary=currentPrimary(state);bool rawDrift=true;
  if(primary){APPBARDATA data{sizeof(data)};data.hWnd=primary;rawDrift=SHAppBarMessage(ABM_GETSTATE,&data)!=state.taskbarState;}
  std::array<NativeBar,32> current{};DWORD count=0;if(!captureRestoreBars(current,count))return false;
  pruneRestoreIdentities(current,count,applied,appliedCount);
  for(DWORD i=0;i<count;++i){const auto& bar=current[i];
   const bool fresh=std::none_of(applied.begin(),applied.begin()+appliedCount,[&](const NativeBar& old){return sameNativeIdentity(old,bar);});
   if(fresh||rawDrift){
    DWORD pid=0;ULONGLONG born=0;
    if(!genuineBar(bar.window,&pid,&born)||pid!=bar.pid||born!=bar.birth)return false;
    APPBARDATA data{sizeof(data)};data.hWnd=bar.window;data.lParam=nativeTaskbarSetterMask(state.taskbarState);SHAppBarMessage(ABM_SETSTATE,&data);
    if(fresh)applied[appliedCount++]=bar;
   }
  }
  const bool targetsRequested=requestRecordedVisibility(state,current,count,targetsApplied,targetsCount);
  const bool areasRequested=restoreCapturedWorkAreas(state,observation);
  auto exact=[&]{
   // Read raw Shell state first, then fresh identity/visibility, geometry last.
   return recoveryRestored(state)&&rescueCoverageConfirmed(state,applied,appliedCount,targetsApplied,targetsCount)&&workAreasRestored(state);
  };
  if(!targetsRequested||!areasRequested||!exact())return false;
  const auto quietUntil=GetTickCount64()+100;if(quietUntil>deadline)return false;
  do{
   Sleep(20);if(!exact())return false;
  }while(GetTickCount64()<quietUntil);
  persistRecoveryDiagnostic(state,"sentinel-rescue-settled","saved-exact-through-100ms",observationStarted);
  return true;
 },10000);
}
HANDLE parseHandle(const wchar_t* text){
 if(!text||!*text)return nullptr;
 for(const wchar_t* cursor=text;*cursor;++cursor)if(*cursor<L'0'||*cursor>L'9')return nullptr;
 wchar_t* end=nullptr;errno=0;auto value=wcstoull(text,&end,10);
 return errno!=ERANGE&&end&&!*end&&value&&value<=std::numeric_limits<std::uintptr_t>::max()?reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value)):nullptr;
}
std::wstring handleText(HANDLE value){return std::to_wstring(reinterpret_cast<std::uintptr_t>(value));}
Child spawnSuspended(const std::filesystem::path& image,std::wstring arguments,const std::vector<HANDLE>& inherited,bool outsideJob){
 Child child;SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
 if(!bytes){preflightDiagnostic("attribute sizing",GetLastError());return child;}
 auto* attributes=static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(),0,bytes));
 if(!attributes){preflightDiagnostic("attribute allocation",GetLastError());return child;}
 if(!InitializeProcThreadAttributeList(attributes,1,0,&bytes)){
  const auto error=GetLastError();HeapFree(GetProcessHeap(),0,attributes);preflightDiagnostic("attribute initialization",error);return child;
 }
 bool ok=true;
 if(!inherited.empty()){
  for(HANDLE handle:inherited)if(!SetHandleInformation(handle,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)){
   const auto error=GetLastError();preflightDiagnostic("handle inheritance",error);ok=false;
  }
  if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,const_cast<HANDLE*>(inherited.data()),inherited.size()*sizeof(HANDLE),nullptr,nullptr)){
   const auto error=GetLastError();preflightDiagnostic("explicit handle list",error);ok=false;
  }
 }
 STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
 BOOL parentJob=FALSE;if(!IsProcessInJob(GetCurrentProcess(),nullptr,&parentJob)){preflightDiagnostic("parent job query",GetLastError());ok=false;}
 DWORD flags=CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT;if(outsideJob&&parentJob)flags|=CREATE_BREAKAWAY_FROM_JOB;
 std::wstring command=L"\""+image.native()+L"\" "+arguments;PROCESS_INFORMATION created{};
 const bool spawned=ok&&CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,!inherited.empty(),flags,nullptr,nullptr,&startup.StartupInfo,&created);
 const auto spawnError=spawned?ERROR_SUCCESS:GetLastError();
 DeleteProcThreadAttributeList(attributes);HeapFree(GetProcessHeap(),0,attributes);
 if(!spawned){
  std::wcerr<<L"DIAG native spawn rejected: image="<<image.native()<<L"; prerequisites="<<ok
   <<L"; outsideJob="<<outsideJob<<L"; parentInJob="<<parentJob<<L"; flags="<<flags
   <<L"; inheritedHandles="<<inherited.size()<<L"; CreateProcessError="<<spawnError<<std::endl;return child;
 }
 child.process.reset(created.hProcess);child.thread.reset(created.hThread);child.pid=created.dwProcessId;child.birth=birthOf(child.process.value);
 const auto birthError=child.birth?ERROR_SUCCESS:GetLastError();BOOL childJob=TRUE;
 const bool jobObserved=!outsideJob||IsProcessInJob(child.process.value,nullptr,&childJob);
 const auto jobError=jobObserved?ERROR_SUCCESS:GetLastError();
 if(!child.birth||(outsideJob&&(!jobObserved||childJob))){
  std::cerr<<"DIAG native child independence rejected: pid="<<child.pid<<"; birth="<<child.birth
   <<"; birthError="<<birthError<<"; jobObserved="<<jobObserved<<"; inJob="<<childJob<<"; jobError="<<jobError<<std::endl;
  TerminateProcess(child.process.value,70);WaitForSingleObject(child.process.value,3000);return {};
 }
 return child;
}bool resume(Child& child){return child.thread&&ResumeThread(child.thread.value)!=static_cast<DWORD>(-1);}
void registerFixture(AcceptanceState& state,const Child& child,LONG kind){
 InterlockedExchange(&state.fixturePid,0);InterlockedExchange(&state.fixtureAccepted,0);InterlockedExchange(&state.helperPid,0);InterlockedExchange64(&state.helperBirth,0);
 InterlockedExchange64(&state.fixtureWindow,0);InterlockedExchange(&state.fixturePhase,0);InterlockedExchange(&state.fixtureKind,kind);
 InterlockedExchange64(&state.fixtureBirth,static_cast<LONG64>(child.birth));InterlockedExchange(&state.fixturePid,static_cast<LONG>(child.pid));
}
Handle openOwned(const AcceptanceState& state,bool helper){
 const auto pid=static_cast<DWORD>(InterlockedCompareExchange(const_cast<volatile LONG*>(helper?&state.helperPid:&state.fixturePid),0,0));
 const auto born=static_cast<ULONGLONG>(InterlockedCompareExchange64(const_cast<volatile LONG64*>(helper?&state.helperBirth:&state.fixtureBirth),0,0));
 if(!pid||!born||pid==GetCurrentProcessId()||pid==state.supervisorPid)return {};
 Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,pid));
 const LONG kind=InterlockedCompareExchange(const_cast<volatile LONG*>(&state.fixtureKind),0,0);if(kind!=1&&kind!=2)return {};
 const auto expected=helper?executablePath().parent_path()/L"AetherWatchdog.exe":kind==2?executablePath().parent_path()/L"AetherUI.exe":executablePath();
 if(!process||birthOf(process.value)!=born||_wcsicmp(imageOf(process.value).c_str(),expected.c_str())!=0)return {};
 return process;
}
bool processEntries(std::vector<PROCESSENTRY32W>& entries){
 entries.clear();Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W item{sizeof(item)};
 if(!snapshot||!Process32FirstW(snapshot.value,&item))return false;
 do{if(entries.size()==8192)return false;entries.push_back(item);}while(Process32NextW(snapshot.value,&item));
 return GetLastError()==ERROR_NO_MORE_FILES;
}
bool holdFixture(const AcceptanceState& state,Child& owned){
 const auto pid=static_cast<DWORD>(InterlockedCompareExchange(const_cast<volatile LONG*>(&state.fixturePid),0,0));
 const auto born=static_cast<ULONGLONG>(InterlockedCompareExchange64(const_cast<volatile LONG64*>(&state.fixtureBirth),0,0));
 if(!pid||!born)return false;auto process=openOwned(state,false);if(!process)return false;
 std::vector<PROCESSENTRY32W> entries;if(!processEntries(entries))return false;
 bool parentKnown=false;
 for(const auto& entry:entries)if(entry.th32ProcessID==pid){parentKnown=entry.th32ParentProcessID==state.supervisorPid;break;}
 if(!parentKnown)return false;
 owned.process=std::move(process);owned.pid=pid;owned.birth=born;return true;
}
bool discoverOwnedWatchdogs(const Child& fixture,std::vector<Handle>& helpers){
 helpers.clear();
 if(!fixture.process||WaitForSingleObject(fixture.process.value,0)!=WAIT_OBJECT_0)return false;
 FILETIME born{},ended{},kernel{},user{};
 if(!GetProcessTimes(fixture.process.value,&born,&ended,&kernel,&user))return false;
 const auto exit=(static_cast<ULONGLONG>(ended.dwHighDateTime)<<32)|ended.dwLowDateTime;if(!exit)return false;
 std::vector<PROCESSENTRY32W> entries;if(!processEntries(entries))return false;
 const auto expected=executablePath().parent_path()/L"AetherWatchdog.exe";
 for(const auto& entry:entries){
  if(entry.th32ParentProcessID!=fixture.pid||_wcsicmp(entry.szExeFile,L"AetherWatchdog.exe")!=0)continue;
  Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,entry.th32ProcessID));
  if(!process){
   // A process gone between enumeration and open cannot remain a writer.
   // Access/observation failures retain the independent recovery owner.
   if(GetLastError()==ERROR_INVALID_PARAMETER)continue;return false;
  }
  const auto created=birthOf(process.value);
  if(!created)return false;
  if(created<fixture.birth||created>exit)continue; // recycled parent PID
  const auto image=imageOf(process.value);
  if(image.empty()||_wcsicmp(image.c_str(),expected.c_str())!=0)return false;
  helpers.push_back(std::move(process));
 }
 return true;
}
bool stopOwned(AcceptanceState& state,Child& fixture){
 if(!fixture.process){
  const auto pid=static_cast<DWORD>(InterlockedCompareExchange(&state.fixturePid,0,0));
  if(!pid)return true;
  if(!holdFixture(state,fixture)){
   std::vector<PROCESSENTRY32W> entries;if(!processEntries(entries))return false;
   bool listed=false;for(const auto& entry:entries)if(entry.th32ProcessID==pid)listed=true;
   // A fixture may run native code only after the sentinel holds its handle.
   // No admission acknowledgement + no remaining process proves no writer.
   return !listed&&InterlockedCompareExchange(&state.fixtureAccepted,0,0)==0;
  }
 }
 const auto status=WaitForSingleObject(fixture.process.value,0);
 if(status==WAIT_FAILED)return false;
 if(status==WAIT_TIMEOUT){
  InterlockedExchange(&state.sentinelTriggered,1);
  if(!TerminateProcess(fixture.process.value,71)||WaitForSingleObject(fixture.process.value,5000)!=WAIT_OBJECT_0)return false;
 }
 // Discovery does not depend on the guard/App publishing its helper PID.
 // The held dead fixture's creation/exit times bound child provenance, and
 // complete enumeration after its death proves that it cannot spawn more.
 std::vector<Handle> helpers;if(!discoverOwnedWatchdogs(fixture,helpers))return false;
 for(auto& helper:helpers){
  InterlockedIncrement(&state.sentinelDiscoveries);
  const auto wait=WaitForSingleObject(helper.value,1500);
  if(wait==WAIT_FAILED)return false;
  if(wait==WAIT_TIMEOUT){
   InterlockedExchange(&state.sentinelTriggered,1);
   if(!TerminateProcess(helper.value,72)||WaitForSingleObject(helper.value,5000)!=WAIT_OBJECT_0)return false;
  }
 }
 return true;
}
int sentinel(int argc,wchar_t** argv){
 if(argc!=6)return 80;
 setConfigDirectory(sentinelDiagnosticProfile(GetCurrentProcessId(),birthOf(GetCurrentProcess())));
 Handle supervisor(parseHandle(argv[2])),mapping(parseHandle(argv[3])),ready(parseHandle(argv[4])),finish(parseHandle(argv[5]));
 if(!supervisor||!mapping||!ready||!finish)return 81;
 auto* state=static_cast<AcceptanceState*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(AcceptanceState)));if(!state)return 82;
 BOOL inJob=TRUE;
 bool safe=validState(state)&&state->supervisorPid==GetProcessId(supervisor.value)&&state->supervisorBirth==birthOf(supervisor.value)
  &&IsProcessInJob(GetCurrentProcess(),nullptr,&inJob)&&!inJob&&barsRestored(*state)&&workAreasRestored(*state);
 if(!safe){UnmapViewOfFile(state);return 83;}
 AcceptanceState original{};
 original.barCount=state->barCount;original.monitorCount=state->monitorCount;original.taskbarState=state->taskbarState;
 original.supervisorPid=state->supervisorPid;original.supervisorBirth=state->supervisorBirth;original.deadline=state->deadline;
 std::copy_n(state->bars,state->barCount,original.bars);std::copy_n(state->monitors,state->monitorCount,original.monitors);
 if(!validState(&original)||!SetEvent(ready.value)){UnmapViewOfFile(state);return 83;}
 Child fixture;HANDLE waits[]{supervisor.value,finish.value};DWORD result=WAIT_TIMEOUT;
 while(result==WAIT_TIMEOUT&&GetTickCount64()<original.deadline){
  if(!fixture.process&&InterlockedCompareExchange(&state->fixturePid,0,0)!=0&&holdFixture(*state,fixture))
   InterlockedExchange(&state->fixtureAccepted,1); // fixture is STILL suspended
  result=WaitForMultipleObjects(2,waits,FALSE,100);
 }
 if(result!=WAIT_OBJECT_0+1)InterlockedExchange(&state->sentinelTriggered,1);
 bool announced=false;
 for(;;){
  // No native fallback can race a still-live or unobservable owned writer.
  // A failed observation/restoration retains this detached owner indefinitely
  // with bounded attempts/backoff; it never abandons an unconfirmed desktop.
  if(stopOwned(*state,fixture)&&directlyRestore(original,state)){
   InterlockedExchange(&state->sentinelRestored,1);UnmapViewOfFile(state);return 0;
  }
  if(!announced){std::cerr<<"Independent taskbar recovery retained: ownership/restoration is not yet confirmed."<<std::endl;announced=true;}
  Sleep(1000);
 }
}
struct FixtureContext {
 TaskbarGuard guard;AcceptanceState* state;HANDLE resumeEvent;HWND window=nullptr;ULONGLONG started=GetTickCount64();
 FixtureContext(AcceptanceState* shared,HANDLE event):guard(TaskbarGuardOptions{true,LeaseMilliseconds}),state(shared),resumeEvent(event){}
 void observeHelper(){auto pid=guard.helperProcessId();auto born=pid?birthOf(pid):0;
  if(pid&&born){InterlockedExchange64(&state->helperBirth,static_cast<LONG64>(born));InterlockedExchange(&state->helperPid,static_cast<LONG>(pid));}}
};
LRESULT CALLBACK fixtureProcedure(HWND window,UINT message,WPARAM w,LPARAM l){
 auto* context=reinterpret_cast<FixtureContext*>(GetWindowLongPtrW(window,GWLP_USERDATA));
 if(message==WM_NCCREATE){context=static_cast<FixtureContext*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(context));}
 if(!context)return DefWindowProcW(window,message,w,l);
 if(message==ArmCommand){
  bool enabled=context->guard.setEnabled(true);context->observeHelper();InterlockedExchange(&context->state->lastCommand,enabled?1L:0L);
  if(enabled){InterlockedExchange(&context->state->fixturePhase,PhaseArmed);SetTimer(window,HeartbeatTimer,100,nullptr);}return enabled?1:0;
 }
 if(message==ArmUnpublishedCommand){
  InterlockedExchange(&context->state->fixturePhase,PhaseArming);
  const bool enabled=context->guard.setEnabled(true);InterlockedExchange(&context->state->lastCommand,enabled?1L:0L);
  InterlockedExchange(&context->state->fixturePhase,PhaseArmUnpublished);
  // Calibration deliberately stops before helper identity publication. The
  // sentinel must discover the actual writer from the held fixture process.
  WaitForSingleObject(context->resumeEvent,15000);
  context->observeHelper();InterlockedExchange(&context->state->fixturePhase,PhaseArmed);SetTimer(window,HeartbeatTimer,100,nullptr);return 0;
 }
 if(message==RestoreCommand){
  KillTimer(window,HeartbeatTimer);const bool restored=context->guard.setEnabled(false);InterlockedExchange(&context->state->lastCommand,restored?1L:0L);
  persistRecoveryDiagnostic(*context->state,"fixture-disable-returned",restored?"disable-true":"disable-false",context->started);return restored?1:0;
 }
 if(message==BlockCommand){
  KillTimer(window,HeartbeatTimer);InterlockedExchange(&context->state->fixturePhase,PhaseBlocked);WaitForSingleObject(context->resumeEvent,15000);
  context->guard.refresh();context->guard.heartbeat();InterlockedExchange(&context->state->fixturePhase,PhaseResumed);SetTimer(window,HeartbeatTimer,100,nullptr);return 0;
 }
 if(message==ProbeCommand){context->guard.refresh();context->guard.heartbeat();return context->guard.enabled()?1:0;}
 if(message==EnableCommand){
  const bool enabled=context->guard.setEnabled(true);InterlockedExchange(&context->state->lastCommand,enabled?1L:0L);context->observeHelper();return enabled?1:0;
 }
 if(message==WM_TIMER&&w==HeartbeatTimer){context->guard.heartbeat();context->guard.refresh();return 0;}
 if(message==CloseCommand||message==WM_CLOSE){
  KillTimer(window,HeartbeatTimer);persistRecoveryDiagnostic(*context->state,"fixture-before-close-disable","read-only",context->started);
  const bool restored=context->guard.setEnabled(false);
  persistRecoveryDiagnostic(*context->state,"fixture-after-close-disable",restored?"disable-true":"disable-false",context->started);
  const bool destroyed=DestroyWindow(window)!=FALSE;
  persistRecoveryDiagnostic(*context->state,"fixture-after-window-destroy",destroyed?"destroy-true":"destroy-false",context->started);return 0;
 }
 if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
int fixture(int argc,wchar_t** argv){
 if(argc!=4)return 85;Handle mapping(parseHandle(argv[2])),resumeEvent(parseHandle(argv[3]));if(!mapping||!resumeEvent)return 86;
 auto* state=static_cast<AcceptanceState*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(AcceptanceState)));
 if(!state||!validState(state)){if(state)UnmapViewOfFile(state);return 87;}int result=0;
 {
  setConfigDirectory(executablePath().parent_path()/L"test-profiles"/(L"native-guard-"+std::to_wstring(GetCurrentProcessId())));
  FixtureContext context(state,resumeEvent.value);WNDCLASSW klass{};klass.hInstance=GetModuleHandleW(nullptr);klass.lpfnWndProc=fixtureProcedure;klass.lpszClassName=L"Aether.Recovery.NativeFixture";
  if(!RegisterClassW(&klass))result=88;
  else{
   context.window=CreateWindowExW(WS_EX_TOOLWINDOW,klass.lpszClassName,L"Aether native recovery fixture",WS_POPUP,0,0,1,1,nullptr,nullptr,klass.hInstance,&context);
   if(!context.window)result=89;
   else{
    InterlockedExchange64(&state->fixtureWindow,static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(context.window)));InterlockedExchange(&state->fixturePhase,PhaseReady);
    MSG message{};while((result=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}
    result=result<0?1:static_cast<int>(message.wParam);
    persistRecoveryDiagnostic(*state,"fixture-message-loop-ended","before-guard-destructor",context.started);
   }
  }
 }
 persistRecoveryDiagnostic(*state,"fixture-before-process-exit","after-guard-destructor");
 UnmapViewOfFile(state);return result;
}
int checks=0,failures=0;
bool check(bool good,const std::string& description){++checks;if(!good)++failures;std::cout<<(good?"PASS ":"FAIL ")<<description<<std::endl;return good;}
class SafeCase {
 Handle mapping_,ready_,finish_,parent_;Child sentinel_;bool beforeCaptured_=false;
public:
 AcceptanceState* state=nullptr;Child worker;Handle resumeEvent;
 explicit SafeCase(DWORD lifetime=SentinelLifetime){
  SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
  mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE,&attributes,PAGE_READWRITE,0,sizeof(AcceptanceState),nullptr));
  if(!mapping_){preflightDiagnostic("shared mapping allocation",GetLastError());return;}
  ready_.reset(CreateEventW(&attributes,TRUE,FALSE,nullptr));
  if(!ready_){preflightDiagnostic("ready event allocation",GetLastError());return;}
  finish_.reset(CreateEventW(&attributes,TRUE,FALSE,nullptr));
  if(!finish_){preflightDiagnostic("finish event allocation",GetLastError());return;}
  resumeEvent.reset(CreateEventW(&attributes,TRUE,FALSE,nullptr));
  if(!resumeEvent){preflightDiagnostic("resume event allocation",GetLastError());return;}
  parent_.reset(OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,GetCurrentProcessId()));
  if(!parent_){preflightDiagnostic("supervisor process handle",GetLastError());return;}
  state=static_cast<AcceptanceState*>(MapViewOfFile(mapping_.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(AcceptanceState)));
  if(!state){preflightDiagnostic("shared mapping view",GetLastError());return;}
  new(state) AcceptanceState{};
  if(!captureBefore(*state)){preflightDiagnostic("captureBefore rejected",GetLastError());return;}
  if(lifetime<SentinelLifetime||lifetime>1800000){preflightDiagnostic("sentinel lifetime rejected",ERROR_INVALID_PARAMETER);return;}
  beforeCaptured_=true;state->deadline=GetTickCount64()+lifetime;
  std::cerr<<"DIAG native capture accepted: bars="<<state->barCount<<"; monitors="<<state->monitorCount
   <<"; fullTaskbarState="<<state->taskbarState<<"; supervisorBirth="<<state->supervisorBirth<<std::endl;
  std::wstring arguments=L"--sentinel "+handleText(parent_.value)+L" "+handleText(mapping_.value)+L" "+handleText(ready_.value)+L" "+handleText(finish_.value);
  sentinel_=spawnSuspended(executablePath(),arguments,{parent_.value,mapping_.value,ready_.value,finish_.value},true);
  if(!sentinel_.process){preflightDiagnostic("sentinel suspended creation rejected",GetLastError());return;}
  if(!resume(sentinel_)){
   const auto error=GetLastError();preflightDiagnostic("sentinel resume rejected",error);
   // This process has never acknowledged or obtained native ownership.
   TerminateProcess(sentinel_.process.value,73);WaitForSingleObject(sentinel_.process.value,3000);return;
  }
  HANDLE startup[]{ready_.value,sentinel_.process.value};
  const auto wait=WaitForMultipleObjects(2,startup,FALSE,5000);const auto waitError=wait==WAIT_FAILED?GetLastError():ERROR_SUCCESS;
  if(wait!=WAIT_OBJECT_0){
   DWORD exit=STILL_ACTIVE;GetExitCodeProcess(sentinel_.process.value,&exit);
   std::cerr<<"DIAG native sentinel startup rejected: pid="<<sentinel_.pid<<"; wait="<<wait
    <<"; waitError="<<waitError<<"; childExit="<<exit<<"; readyEvent="
    <<(WaitForSingleObject(ready_.value,0)==WAIT_OBJECT_0)<<std::endl;return;
  }
  std::cout<<"Sentinel "<<sentinel_.pid<<" acknowledged outside the supervisor job; captured "<<state->barCount<<" genuine taskbars and "<<state->monitorCount<<" physical monitor work areas."<<std::endl;
  std::cout<<"DIAG persisted sentinel log: "<<utf8((sentinelDiagnosticProfile(sentinel_.pid,sentinel_.birth)/L"aether.log").wstring())<<std::endl;
 }
 bool ready()const{return beforeCaptured_&&validState(state)&&sentinel_.alive()&&ready_&&WaitForSingleObject(ready_.value,0)==WAIT_OBJECT_0;}
 bool launchGuard(){
  if(!ready())return false;std::wstring arguments=L"--guard-fixture "+handleText(mapping_.value)+L" "+handleText(resumeEvent.value);
  worker=spawnSuspended(executablePath(),arguments,{mapping_.value,resumeEvent.value},false);
  if(!worker.process)return false;registerFixture(*state,worker,1);if(!waitFor([&]{return InterlockedCompareExchange(&state->fixtureAccepted,0,0)==1;},5000)||!resume(worker))return false;
  return waitFor([&]{return InterlockedCompareExchange(&state->fixturePhase,0,0)==PhaseReady;},5000);
 }
 bool launchApp(const wchar_t* name,bool hideTaskbar=true,bool hideFullscreen=true,bool autoHide=false,int sleekMode=-1){
  if(!ready())return false;
  const auto directory=executablePath().parent_path()/L"test-profiles"/(L"native-taskbar-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+name);
  setConfigDirectory(directory);Config configuration;configuration.hideTaskbar=hideTaskbar;configuration.hideFullscreen=hideFullscreen;configuration.defaults.autoHide=autoHide;configuration.pins.clear();configuration.tilingDefaults.enabled=false;saveConfig(configuration);
  if(sleekMode>=0){configuration.theme=aether::preset(5);if(sleekMode>=2){configuration.theme.style=ThemeStyle::Frosted;configuration.theme.topBarStyle=BarStyle::Islands;configuration.theme.dockStyle=BarStyle::Islands;configuration.theme.topBarHeight=46;configuration.theme.barPadding=8;}configuration.animatedWallpaper=sleekMode==2?1:0;saveConfig(configuration);if(sleekMode==1){ExtensionLibrary library;std::string error;if(!library.load(&error))return false;for(const auto& package:{aether::sleekDeskExtension(0),aether::productivityExtension()}){auto id=library.install(package,&error);if(!id||!library.setEnabled(*id,true,&error))return false;}}}
  std::wstring arguments=L"--settings-dir \""+directory.native()+L"\"";
  worker=spawnSuspended(executablePath().parent_path()/L"AetherUI.exe",arguments,{},false);
  if(!worker.process)return false;registerFixture(*state,worker,2);if(!waitFor([&]{return InterlockedCompareExchange(&state->fixtureAccepted,0,0)==1;},5000)||!resume(worker))return false;
  return waitFor([&]{
   struct Search{DWORD pid;HWND found=nullptr;};Search search{worker.pid};
   EnumWindows([](HWND window,LPARAM value)->BOOL{
    auto& search=*reinterpret_cast<Search*>(value);DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t name[64]{};GetClassNameW(window,name,64);
    if(pid==search.pid&&wcscmp(name,L"Aether.Controller.v3")==0){search.found=window;return FALSE;}return TRUE;
   },reinterpret_cast<LPARAM>(&search));
   if(search.found)InterlockedExchange64(&state->fixtureWindow,static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(search.found)));
   return search.found&&reinterpret_cast<ULONG_PTR>(GetPropW(search.found,L"Aether.LifecyclePhase"))==1;
  },15000);
 }
 HWND window()const{return state?reinterpret_cast<HWND>(static_cast<std::uintptr_t>(InterlockedCompareExchange64(&state->fixtureWindow,0,0))):nullptr;}
 bool command(UINT message,DWORD_PTR* result=nullptr,DWORD timeout=8000,WPARAM w=0,LPARAM l=0){
  DWORD owner=0;if(window())GetWindowThreadProcessId(window(),&owner);
  DWORD_PTR reply=0;const bool sent=worker.alive()&&birthOf(worker.process.value)==worker.birth&&owner==worker.pid
   &&SendMessageTimeoutW(window(),message,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,timeout,&reply)!=0;
  if(result)*result=reply;return sent;
 }
 DWORD sentinelPid()const{return sentinel_.pid;}
 bool sentinelAlive()const{return sentinel_.alive();}
 bool sentinelCompleted()const{
  DWORD exit=STILL_ACTIVE;
  return sentinel_.process&&WaitForSingleObject(sentinel_.process.value,0)==WAIT_OBJECT_0
   &&GetExitCodeProcess(sentinel_.process.value,&exit)&&exit==0&&state
   &&InterlockedCompareExchange(&state->sentinelRestored,0,0)==1;
 }
 bool ownsRecoveryState()const{return beforeCaptured_&&validState(state)&&worker.process;}
 bool arm(){DWORD_PTR reply=0;return command(ArmCommand,&reply)&&reply==1&&waitFor([&]{return barsHidden(*state);},5000);}
 Handle helper()const{return state?openOwned(*state,true):Handle{};}
 bool observeAppHelper(){
  return waitFor([&]{
   Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));PROCESSENTRY32W item{sizeof(item)};
   if(snapshot&&Process32FirstW(snapshot.value,&item))do{
    if(item.th32ParentProcessID==worker.pid&&_wcsicmp(item.szExeFile,L"AetherWatchdog.exe")==0){
     auto born=birthOf(item.th32ProcessID);
     if(born){InterlockedExchange64(&state->helperBirth,static_cast<LONG64>(born));InterlockedExchange(&state->helperPid,static_cast<LONG>(item.th32ProcessID));return static_cast<bool>(helper());}
    }
   }while(Process32NextW(snapshot.value,&item));return false;
  },5000);
 }
 bool finish(const std::string& label,bool expectedEmergency=false){
  if(resumeEvent)SetEvent(resumeEvent.value);
  if(worker.alive()){
   if(InterlockedCompareExchange(&state->fixtureKind,0,0)==2){if(window())PostMessageW(window(),WM_CLOSE,0,0);}
   else if(window())PostMessageW(window(),CloseCommand,0,0);
   WaitForSingleObject(worker.process.value,5000);
  }
  bool clean=ownsRecoveryState()&&!worker.alive();if(clean&&!expectedEmergency)clean=waitFor([&]{return barsRestored(*state)&&workAreasRestored(*state);},8000);
  check(clean,label+(expectedEmergency?": publication-window fixture is dead before failsafe recovery":": fixture stopped and exact original native visibility, full appbar state and physical work areas independently restored"));
  if(state&&!clean&&!expectedEmergency){describeWorkAreas(*state,"failed-original-cleanup-before-failsafe");InterlockedExchange(&state->sentinelTriggered,1);}
  if(finish_)SetEvent(finish_.value);
  bool sentinelExited=sentinel_.process&&WaitForSingleObject(sentinel_.process.value,20000)==WAIT_OBJECT_0;
  DWORD exit=STILL_ACTIVE;if(sentinelExited)GetExitCodeProcess(sentinel_.process.value,&exit);
  if(ownsRecoveryState()&&!sentinel_.alive()&&!sentinelCompleted()){
   InterlockedExchange(&state->sentinelTriggered,1);bool announced=false;
   for(;;){
    // Sentinel loss is a failed observation, never a reason to abandon the
    // owned App. Hold its verified process, retire every discovered owned
    // writer, then restore. Uncertainty retains this supervisor as owner.
    if(stopOwned(*state,worker)&&directlyRestore(*state,state))break;
    if(!announced){std::cerr<<"Supervisor retained: independent sentinel unavailable and fallback restoration is unconfirmed."<<std::endl;announced=true;}
    Sleep(1000);
   }
   check(false,label+": independent sentinel unavailable; supervisor performed confirmed native fallback recovery");
  }
  check(sentinelExited&&exit==0&&state&&InterlockedCompareExchange(&state->sentinelRestored,0,0)==1,label+": independent failsafe exits only after confirmed native restoration");
  check(state&&InterlockedCompareExchange(&state->sentinelTriggered,0,0)==(expectedEmergency?1L:0L),
   label+(expectedEmergency?": calibrated failsafe intervention was observed":": normal outcome required no emergency failsafe intervention"));
  if(expectedEmergency){
   check(state&&InterlockedCompareExchange(&state->sentinelDiscoveries,0,0)>0,label+": unpublished actual watchdog was discovered by provenance, independently of the missing PID publication");
   check(state&&barsRestored(*state)&&workAreasRestored(*state),label+": exact original visibility, complete appbar state and all work areas restored after the calibrated failsafe");
  }
  return clean&&sentinelExited&&exit==0;
 }
 ~SafeCase(){
  if(ownsRecoveryState()&&!sentinel_.alive()&&!sentinelCompleted()){
   if(resumeEvent)SetEvent(resumeEvent.value);
   // Early admission failures still retire the owned process and its actual
   // writers before native fallback; unconfirmed recovery keeps this owner.
   while(!(stopOwned(*state,worker)&&directlyRestore(*state,state)))Sleep(1000);
  }
  if(resumeEvent)SetEvent(resumeEvent.value);
  if(sentinel_.alive()){
   if(state&&!barsRestored(*state))InterlockedExchange(&state->sentinelTriggered,1);
   if(finish_)SetEvent(finish_.value);WaitForSingleObject(sentinel_.process.value,20000);
  }
  // A detached sentinel that still lives retains its mapping/parent handle and
  // hard deadline. Never kill the last independent restoration owner.
  if(state)UnmapViewOfFile(state);
 }
};
std::vector<Handle> suspendOwnedHelper(HANDLE helper,DWORD pid){
 std::vector<Handle> suspended;if(!helper||GetProcessId(helper)!=pid||WaitForSingleObject(helper,0)!=WAIT_TIMEOUT)return suspended;
 Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0));THREADENTRY32 item{sizeof(item)};
 if(snapshot&&Thread32First(snapshot.value,&item))do{
  if(item.th32OwnerProcessID==pid){
   Handle thread(OpenThread(THREAD_SUSPEND_RESUME|THREAD_QUERY_LIMITED_INFORMATION,FALSE,item.th32ThreadID));
   if(thread&&GetProcessIdOfThread(thread.value)==pid&&SuspendThread(thread.value)!=static_cast<DWORD>(-1))suspended.push_back(std::move(thread));
  }
 }while(Thread32Next(snapshot.value,&item));return suspended;
}
void resumeOwnedThreads(std::vector<Handle>& threads){for(auto& thread:threads)ResumeThread(thread.value);threads.clear();}
bool admission(SafeCase& test,const char* scenario){
 if(!check(test.ready(),std::string(scenario)+": immutable before-state and independent outside-job sentinel acknowledged"))return false;
 if(!check(test.launchGuard(),std::string(scenario)+": isolated production guard fixture ready"))return false;
 return check(test.arm(),std::string(scenario)+": actual acknowledged helper hides every genuine native taskbar and enables auto-hide");
}
void normalDisableCase(bool holdAlive=false){
 SafeCase test;if(!admission(test,"normal disable"))return;DWORD_PTR reply=0;
 check(test.command(RestoreCommand,&reply)&&reply==1,"normal disable: bounded guard acknowledgement");
 check(waitFor([&]{return barsRestored(*test.state);},5000),"normal disable: exact native before-state restored before fixture exit");
 if(holdAlive){
  // Separate diagnostic experiment only. Existing full/isolated cases do not
  // acquire this extra wait. No Shell state query or restoration is introduced.
  const auto started=GetTickCount64();
  std::cout<<"DIAG hold-alive observer profile: "<<utf8(configDirectory().wstring())<<std::endl;
  for(unsigned sample=0;sample<=15;++sample){
   std::cout<<"DIAG held-after-disable sample="<<sample<<" elapsedMs="<<GetTickCount64()-started<<" fixtureAlive="<<test.worker.alive()<<std::endl;
   describeWorkAreas(*test.state,"hold-alive-after-disable");
   persistRecoveryDiagnostic(*test.state,"hold-alive-after-disable",test.worker.alive()?"fixture-alive":"fixture-exited",started);
   if(sample<15)Sleep(100);
  }
 }
 test.finish("normal disable");
}
void normalExitCase(){
 SafeCase test;if(!admission(test,"normal exit"))return;if(test.window())PostMessageW(test.window(),CloseCommand,0,0);
 check(WaitForSingleObject(test.worker.process.value,8000)==WAIT_OBJECT_0,"normal exit: fixture exits cooperatively");test.finish("normal exit");
}
void parentCrashCase(){
 SafeCase test;if(!admission(test,"parent crash"))return;auto helper=test.helper();
 check(static_cast<bool>(helper),"parent crash: watchdog PID and birth validated before termination");
 check(TerminateProcess(test.worker.process.value,91)&&WaitForSingleObject(test.worker.process.value,5000)==WAIT_OBJECT_0,"parent crash: only the owned fixture is forcibly ended");
 check(waitFor([&]{return barsRestored(*test.state);},10000),"parent crash: actual independent watchdog restores all native taskbars and full appbar state");
 check(helper&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,"parent crash: watchdog completes recovery and exits");test.finish("parent crash");
}
void hungParentCase(){
 SafeCase test;if(!admission(test,"hung parent and resume"))return;auto helper=test.helper();
 check(test.window()&&PostMessageW(test.window(),BlockCommand,0,0),"hung parent: block command submitted to owned UI thread");
 check(waitFor([&]{return InterlockedCompareExchange(&test.state->fixturePhase,0,0)==PhaseBlocked;},3000),"hung parent: heartbeat stops inside blocked UI thread");
 DWORD_PTR unused=0;check(!test.command(WM_NULL,&unused,150),"hung parent: actual fixture message loop is unresponsive");
 check(test.worker.alive()&&waitFor([&]{return barsRestored(*test.state);},10000),"hung parent: watchdog restores while its parent remains alive");
 check(helper&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,"hung parent: expired helper lifetime ends after confirmed restoration");
 SetEvent(test.resumeEvent.value);
 check(waitFor([&]{return InterlockedCompareExchange(&test.state->fixturePhase,0,0)==PhaseResumed;},3000),"resume: owned UI thread resumes and calls normal refresh and heartbeat");
 DWORD_PTR enabled=1;check(test.command(EnableCommand,&enabled)&&enabled==0,"resume: timed-out guard refuses implicit re-arm after recovery");
 bool stayedRestored=true;const auto until=GetTickCount64()+2500;
 while(GetTickCount64()<until){DWORD_PTR result=1;if(!test.command(ProbeCommand,&result)||result!=0||!barsRestored(*test.state))stayedRestored=false;Sleep(50);}
 check(stayedRestored,"resume: repeated reconciliation and heartbeat never hide restored taskbars again");test.finish("hung parent and resume");
}
void helperCrashCase(bool holdAlive=false){
 SafeCase test;if(!admission(test,"helper crash"))return;auto helper=test.helper();
 check(helper&&TerminateProcess(helper.value,92)&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,"helper crash: only the recorded watchdog is terminated and death confirmed");
 check(waitFor([&]{test.command(ProbeCommand);return barsRestored(*test.state);},8000),"helper crash: responsive controller fallback restores exact native before-state after helper death");
 if(holdAlive){
  // Separate diagnostic experiment only: keep the recovered fixture alive.
  // No extra wait, Shell state query or repair enters the default full case.
  const auto started=GetTickCount64();
  std::cout<<"DIAG helper-crash hold observer profile: "<<utf8(configDirectory().wstring())<<std::endl;
  for(unsigned sample=0;sample<=15;++sample){
   std::cout<<"DIAG held-after-helper-crash sample="<<sample<<" elapsedMs="<<GetTickCount64()-started<<" fixtureAlive="<<test.worker.alive()<<std::endl;
   describeWorkAreas(*test.state,"hold-alive-after-helper-crash");
   persistRecoveryDiagnostic(*test.state,"hold-alive-after-helper-crash",test.worker.alive()?"fixture-alive":"fixture-exited",started);
   if(sample<15)Sleep(100);
  }
 }
 DWORD_PTR enabled=1;check(test.command(EnableCommand,&enabled)&&enabled==0,"helper crash: failed recovery generation cannot silently re-arm");test.finish("helper crash");
}
void helperHangCase(){
 SafeCase test;if(!admission(test,"helper hang"))return;auto helper=test.helper();const auto pid=helper?GetProcessId(helper.value):0;
 auto threads=suspendOwnedHelper(helper.value,pid);if(!check(!threads.empty(),"helper hang: only threads of the PID/birth-validated watchdog are suspended"))return;
 const auto begin=GetTickCount64();DWORD_PTR restored=0;const bool acknowledged=test.command(RestoreCommand,&restored,12000);
 check(acknowledged&&restored==1&&GetTickCount64()-begin<12000,"helper hang: controller disable has a bounded successful acknowledgement");
 check(helper&&WaitForSingleObject(helper.value,0)==WAIT_OBJECT_0,"helper hang: nonresponsive helper is confirmed dead before native fallback");
 check(waitFor([&]{return barsRestored(*test.state);},5000),"helper hang: controller fallback restores all original native state");
 resumeOwnedThreads(threads);test.finish("helper hang");
}
Handle unpublishedHelper(const Child& fixture){
 if(!fixture.alive())return {};
 std::vector<PROCESSENTRY32W> entries;if(!processEntries(entries))return {};
 const auto expected=executablePath().parent_path()/L"AetherWatchdog.exe";
 for(const auto& entry:entries)if(entry.th32ParentProcessID==fixture.pid&&_wcsicmp(entry.szExeFile,L"AetherWatchdog.exe")==0){
  Handle helper(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE,FALSE,entry.th32ProcessID));
  if(helper&&birthOf(helper.value)>=fixture.birth&&_wcsicmp(imageOf(helper.value).c_str(),expected.c_str())==0&&WaitForSingleObject(helper.value,0)==WAIT_TIMEOUT)return helper;
 }
 return {};
}
void describeArmFailure(const SafeCase& test){
 const auto* state=test.state;
 if(!state){preflightDiagnostic("Arm failure has no shared state",ERROR_INVALID_DATA);return;}
 std::cerr<<"DIAG native Arm observation: fixturePid="<<test.worker.pid<<"; fixtureBirth="<<test.worker.birth
  <<"; fixtureAlive="<<test.worker.alive()<<"; phase="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&state->fixturePhase),0,0)
  <<"; lastCommand="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&state->lastCommand),0,0)
  <<"; publishedHelperPid="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&state->helperPid),0,0)
  <<"; publishedHelperBirth="<<InterlockedCompareExchange64(const_cast<volatile LONG64*>(&state->helperBirth),0,0)
  <<"; expectedHiddenMask="<<(state->taskbarState|ABS_AUTOHIDE)<<"; originalMask="<<state->taskbarState<<std::endl;
 const auto primary=currentPrimary(*state);APPBARDATA data{sizeof(data)};data.hWnd=primary;
 std::cerr<<"DIAG native Arm desktop: primary="<<reinterpret_cast<std::uintptr_t>(primary)
  <<"; observedMask="<<(primary?SHAppBarMessage(ABM_GETSTATE,&data):static_cast<UINT_PTR>(-1))
  <<"; barSetUnchanged="<<barSetUnchanged(*state)<<"; currentlyHidden="<<barsHidden(*state)<<std::endl;
 describeNativeBars();
 std::vector<PROCESSENTRY32W> children;
 if(!processEntries(children)){preflightDiagnostic("Arm child enumeration failed",GetLastError());return;}
 DWORD count=0;
 for(const auto& entry:children)if(entry.th32ParentProcessID==test.worker.pid){
  ++count;Handle child(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID));
  const auto openError=child?ERROR_SUCCESS:GetLastError();
  std::wcerr<<L"DIAG native Arm child: pid="<<entry.th32ProcessID<<L"; name="<<entry.szExeFile
   <<L"; birth="<<(child?birthOf(child.value):0)<<L"; image="<<(child?imageOf(child.value):std::wstring{})
   <<L"; status="<<(child?WaitForSingleObject(child.value,0):WAIT_FAILED)<<L"; openError="<<openError<<std::endl;
 }
 std::cerr<<"DIAG native Arm child enumeration: complete=1; observedChildren="<<count<<std::endl;
}
void unpublishedArmFailsafeCase(){
 SafeCase test;
 if(!check(test.ready(),"failsafe calibration: immutable before-state and independent sentinel acknowledged"))return;
 if(!check(test.launchGuard(),"failsafe calibration: sentinel holds the still-suspended fixture before granting admission"))return;
 std::cerr<<"DIAG failsafe calibration: physical pointer/taskbar bounds before Arm; no input is synthesized."<<std::endl;
 describeNativeBars();
 check(test.window()&&PostMessageW(test.window(),ArmUnpublishedCommand,0,0),"failsafe calibration: Arm enters the unacknowledged identity-publication window");
 if(!check(waitFor([&]{return InterlockedCompareExchange(&test.state->fixturePhase,0,0)==PhaseArmUnpublished&&InterlockedCompareExchange(&test.state->lastCommand,0,0)==1&&barsHidden(*test.state);},8000),
  "failsafe calibration: actual native hide completed before helper PID was published")){describeArmFailure(test);return;}
 check(InterlockedCompareExchange(&test.state->helperPid,0,0)==0,"failsafe calibration: shared helper PID remains absent");
 Handle helper;waitFor([&]{helper=unpublishedHelper(test.worker);return static_cast<bool>(helper);},3000);
 if(!check(static_cast<bool>(helper),"failsafe calibration: observer independently validates actual child helper image, PID and birth"))return;
 auto threads=suspendOwnedHelper(helper.value,GetProcessId(helper.value));
 if(!check(!threads.empty(),"failsafe calibration: suspend only the verified unpublished writer"))return;
 check(TerminateProcess(test.worker.process.value,94)&&WaitForSingleObject(test.worker.process.value,5000)==WAIT_OBJECT_0,
  "failsafe calibration: terminate only the fixture inside the Arm identity-publication window");
 test.finish("Arm publication-window failsafe",true);
 check(WaitForSingleObject(helper.value,0)==WAIT_OBJECT_0,"failsafe calibration: unpublished native writer is confirmed dead before the recovery owner exits");
 resumeOwnedThreads(threads);
}
 int guardAcceptance(){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before native acceptance.\n";return 2;}
 unpublishedArmFailsafeCase();if(failures)return 1;normalDisableCase();if(failures)return 1;normalExitCase();if(failures)return 1;parentCrashCase();if(failures)return 1;
 hungParentCase();if(failures)return 1;helperCrashCase();if(failures)return 1;helperHangCase();
 std::cout<<checks-failures<<"/"<<checks<<" actual native taskbar recovery checks passed."<<std::endl;return failures?1:0;
}
int isolatedGuardAcceptance(bool calibration,bool holdAlive=false){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before isolated native acceptance.\n";return 2;}
 if(holdAlive)setConfigDirectory(executablePath().parent_path()/L"test-profiles"/(L"native-hold-observer-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(birthOf(GetCurrentProcess()))));
 if(calibration)unpublishedArmFailsafeCase();else normalDisableCase(holdAlive);
 std::cout<<checks-failures<<"/"<<checks<<(calibration?" isolated native calibration":holdAlive?" isolated native normal-disable hold experiment":" isolated native normal-disable")<<" checks passed."<<std::endl;
 return failures?1:0;
}
int isolatedHelperCrashAcceptance(bool holdAlive=false){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before isolated native acceptance.\n";return 2;}
 if(holdAlive)setConfigDirectory(executablePath().parent_path()/L"test-profiles"/(L"native-helper-crash-observer-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(birthOf(GetCurrentProcess()))));
 helperCrashCase(holdAlive);
 std::cout<<checks-failures<<"/"<<checks<<(holdAlive?" isolated native helper-crash hold experiment":" isolated native helper-crash")<<" checks passed."<<std::endl;
 return failures?1:0;
}
unsigned liveSceneCount(DWORD pid){
 struct Observation{DWORD pid;unsigned count=0;} observation{pid};
 auto child=[](HWND h,LPARAM data)->BOOL{auto& o=*reinterpret_cast<Observation*>(data);DWORD owner=0;GetWindowThreadProcessId(h,&owner);if(owner!=o.pid||windowText(h)!=L"Aether animated wallpaper")return TRUE;wchar_t klass[32]{};GetClassNameW(GetParent(h),klass,32);if(std::wcscmp(klass,L"WorkerW")==0&&GetPropW(h,L"Aether.LiveWallpaper.Frames"))++o.count;return TRUE;};
 struct Walk{WNDENUMPROC child;Observation* observation;} walk{child,&observation};
 EnumWindows([](HWND h,LPARAM data)->BOOL{auto& w=*reinterpret_cast<Walk*>(data);EnumChildWindows(h,w.child,reinterpret_cast<LPARAM>(w.observation));return TRUE;},reinterpret_cast<LPARAM>(&walk));return observation.count;
}
bool savedStartupHiding(bool requested){auto text=readText(configDirectory()/L"settings-v3.ini");return !text.empty()&&parseSettings(text).hideTaskbar==requested;}
HWND ownedEditor(DWORD root){struct Query{DWORD pid;HWND found=nullptr;} query{root};EnumWindows([](HWND h,LPARAM data)->BOOL{auto& q=*reinterpret_cast<Query*>(data);DWORD owner=0;GetWindowThreadProcessId(h,&owner);if(owner==q.pid&&windowText(h)==L"Aether UI - Desktop Studio"){q.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&query));return query.found;}
bool editorAction(HWND editor,const wchar_t* name){
 ComPtr<IAccessible> tree;if(FAILED(AccessibleObjectFromWindow(editor,static_cast<DWORD>(OBJID_CLIENT),IID_IAccessible,reinterpret_cast<void**>(tree.GetAddressOf()))))return false;
 long count=0;tree->get_accChildCount(&count);
 for(long id=1;id<=count;++id){VARIANT child{};child.vt=VT_I4;child.lVal=id;BSTR text=nullptr;tree->get_accName(child,&text);bool match=text&&std::wstring_view(text,SysStringLen(text))==name;SysFreeString(text);if(match)return tree->accDoDefaultAction(child)==S_OK;}
 return false;
}
void appCase(const wchar_t* name,bool emergency,bool crash,int effects=-1){
 SafeCase test;if(!check(test.ready(),"actual App: before-state and independent outside-job sentinel acknowledged"))return;
 if(!check(test.launchApp(name,true,true,false,effects),"actual App: private-profile process reaches its Ready lifecycle"))return;
 if(effects==2)check(waitFor([&]{return liveSceneCount(test.worker.pid)==test.state->monitorCount;},4000),"actual effects App: every physical monitor has a painted owned desktop scene");
 check(test.observeAppHelper(),"actual App: actual watchdog PID, birth and image captured");
 check(waitFor([&]{return barsHidden(*test.state);},8000),"actual App: Windows taskbars are hidden at startup without a toggle");
 DWORD_PTR reply=0;check(test.command(WM_NULL,&reply,500),"actual App: controller remains responsive while replacement is active");
 DWORD_PTR settling=0;check(test.command(RegisterWindowMessageW(L"Aether.Appearance.Transition.v1"),&settling,1500)&&settling==1,"actual App: normal exit, emergency or crash retains priority during appearance settlement");
 if(emergency){if(test.window())PostMessageW(test.window(),ApplyMessage,0,0);
  check(waitFor([&]{return barsRestored(*test.state);},8000),"actual App: emergency command restores exact native visibility and full appbar state");}
 if(crash){auto helper=test.helper();
  check(TerminateProcess(test.worker.process.value,93)&&WaitForSingleObject(test.worker.process.value,5000)==WAIT_OBJECT_0,"actual App crash: only the owned private-profile process is terminated");
  check(waitFor([&]{return barsRestored(*test.state);},10000),"actual App crash: watchdog restores native taskbars");
  check(helper&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,"actual App crash: watchdog retires after recovery");}
 test.finish(crash?"actual App crash":emergency?"actual App emergency":"actual App normal exit");
 if(emergency)check(savedStartupHiding(false),"explicit emergency restore turns off the saved hiding preference");
 if(effects==2)check(liveSceneCount(test.worker.pid)==0,"actual effects App: owned wallpaper windows are gone after retirement");
}
class SuspendedController {
 Handle thread_;bool suspended_=false;
public:
 SuspendedController(const Child& app,HWND window){
  DWORD pid=0;auto thread=window?GetWindowThreadProcessId(window,&pid):0;
  if(!thread||pid!=app.pid||!app.alive()||birthOf(app.process.value)!=app.birth)return;
  thread_.reset(OpenThread(THREAD_SUSPEND_RESUME|THREAD_QUERY_LIMITED_INFORMATION,FALSE,thread));
  if(thread_&&GetProcessIdOfThread(thread_.value)==app.pid&&SuspendThread(thread_.value)!=static_cast<DWORD>(-1))suspended_=true;
 }
 bool active()const{return suspended_;}
 bool resume(){
  if(!suspended_)return true;const bool resumed=ResumeThread(thread_.value)!=static_cast<DWORD>(-1);
  if(resumed)suspended_=false;return resumed;
 }
 ~SuspendedController(){resume();}
};
bool appAdmission(SafeCase& test,const wchar_t* name,const char* label,bool hideFullscreen=true,int sleekMode=-1){
 if(!check(test.ready(),std::string(label)+": immutable before-state and independent sentinel acknowledged"))return false;
 if(!check(test.launchApp(name,true,hideFullscreen,false,sleekMode),std::string(label)+": actual private-profile App reaches Ready"))return false;
 if(!check(test.observeAppHelper(),std::string(label)+": actual watchdog PID, image and birth verified"))return false;
 return check(waitFor([&]{return barsHidden(*test.state);},8000),std::string(label)+": Windows taskbars are hidden without a startup toggle");
}
void appHelperCrashCase(){
 SafeCase test;if(!appAdmission(test,L"helper-death","actual App helper death"))return;auto helper=test.helper();
 DWORD_PTR settling=0;check(test.command(RegisterWindowMessageW(L"Aether.Appearance.Transition.v1"),&settling,1500)&&settling==1,"actual App helper death: bounded appearance settlement acknowledged before helper termination");
 check(helper&&TerminateProcess(helper.value,95)&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,
  "actual App helper death: only the verified private-profile watchdog is terminated");
 check(test.worker.alive()&&waitFor([&]{return barsRestored(*test.state);},8000),
  "actual App helper death: actual RecoveryTimer restores native bars and full state without sentinel intervention");
 DWORD_PTR reply=0;check(test.command(WM_NULL,&reply,500),"actual App helper death: controller remains responsive after fallback");
 bool stable=true;const auto until=GetTickCount64()+2500;
 while(GetTickCount64()<until){if(!barsRestored(*test.state))stable=false;Sleep(50);}
 check(stable,"actual App helper death: normal timers never silently re-hide restored taskbars");
 check(savedStartupHiding(true),"automatic helper recovery preserves hide-on-load preference");
 // Exercise the real post-recovery save and Resume controls through native
 // accessibility, without injecting pointer/keyboard input into other apps.
 ComApartment apartment;HWND editor=nullptr;
 check(test.command(OpenEditorMessage),"recovery preferences: open only the owned App Studio");
 if(check(waitFor([&]{editor=ownedEditor(test.worker.pid);return editor&&IsWindowVisible(editor);},8000),"recovery preferences: owned Studio appears")){
  check(editorAction(editor,L"Behavior & recovery"),"recovery preferences: select behavior controls");
  const bool before=parseSettings(readText(configDirectory()/L"settings-v3.ini")).clock24;
  check(waitFor([&]{return editorAction(editor,L"24-hour clock");},3000),"recovery preferences: change an unrelated clock setting");
  check(waitFor([&]{auto text=readText(configDirectory()/L"settings-v3.ini");auto value=parseSettings(text);return !text.empty()&&value.hideTaskbar&&value.clock24!=before;},3000),"unrelated settings save cannot erase recovered startup preference");
  check(editorAction(editor,L"Resume taskbar hiding"),"recovery preferences: explicitly resume hiding");
  check(test.observeAppHelper(),"explicit Resume publishes a fresh verified watchdog");
  check(waitFor([&]{return barsHidden(*test.state);},5000),"explicit Resume hides all native taskbars again");
 }
 test.finish("actual App helper death and explicit resume");
}
void appControllerHangCase(){
 SafeCase test;if(!appAdmission(test,L"ui-hang","actual App UI hang"))return;auto helper=test.helper();
 DWORD_PTR settling=0;check(test.command(RegisterWindowMessageW(L"Aether.Appearance.Transition.v1"),&settling,1500)&&settling==1,"actual App UI hang: appearance settlement does not replace the controller lease");
 {
  SuspendedController blocked(test.worker,test.window());
  if(!check(blocked.active(),"actual App UI hang: suspend only the HWND/PID/birth-verified controller thread"))return;
  DWORD_PTR reply=0;check(!test.command(WM_NULL,&reply,150),"actual App UI hang: actual controller becomes unresponsive");
  check(test.worker.alive()&&waitFor([&]{return barsRestored(*test.state);},12000),
   "actual App UI hang: actual watchdog restores native bars while its App remains alive");
  check(helper&&WaitForSingleObject(helper.value,5000)==WAIT_OBJECT_0,"actual App UI hang: watchdog retires after lease recovery");
  check(blocked.resume(),"actual App UI hang: verified controller thread resumes through the RAII owner");
 }
 DWORD_PTR reply=0;check(waitFor([&]{return test.command(WM_NULL,&reply,300);},3000),"actual App resume: controller responds again");
 bool stable=true;const auto until=GetTickCount64()+2500;
 while(GetTickCount64()<until){if(!barsRestored(*test.state))stable=false;Sleep(50);}
 check(stable,"actual App resume: queued timers and reconciliation never re-hide the restored taskbar");
 check(savedStartupHiding(true),"automatic UI-hang recovery preserves hide-on-load preference");
 test.finish("actual App UI hang and resume");
}
void describeSyntheticSession(const SafeCase& test,const Handle& expectedHelper,const char* stage,ULONGLONG started){
 const auto* before=test.state;if(!before)return;
 const auto tick=GetTickCount64();const auto primary=currentPrimary(*before);APPBARDATA data{sizeof(data)};data.hWnd=primary;
 std::cerr<<"DIAG synthetic session: stage="<<stage<<"; tick="<<tick<<"; elapsedMs="<<(tick-started)
  <<"; AppPid="<<test.worker.pid<<"; AppBirth="<<test.worker.birth<<"; AppAlive="<<test.worker.alive()
  <<"; capturedHelperPid="<<InterlockedCompareExchange(const_cast<volatile LONG*>(&before->helperPid),0,0)
  <<"; capturedHelperBirth="<<InterlockedCompareExchange64(const_cast<volatile LONG64*>(&before->helperBirth),0,0)
  <<"; heldHelperPid="<<(expectedHelper?GetProcessId(expectedHelper.value):0)
  <<"; heldHelperBirth="<<(expectedHelper?birthOf(expectedHelper.value):0)
  <<"; heldHelperWait="<<(expectedHelper?WaitForSingleObject(expectedHelper.value,0):WAIT_FAILED)
  <<"; observedMask="<<(primary?SHAppBarMessage(ABM_GETSTATE,&data):static_cast<UINT_PTR>(-1))
  <<"; originalMask="<<before->taskbarState<<"; expectedHiddenMask="<<(before->taskbarState|ABS_AUTOHIDE);
 for(DWORD i=0;i<before->barCount;++i){
  const auto& bar=before->bars[i];const auto window=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(bar.hwnd));
  DWORD owner=0;const auto thread=GetWindowThreadProcessId(window,&owner);RECT bounds{};const bool rectObserved=GetWindowRect(window,&bounds)!=FALSE;
  std::cerr<<" [bar="<<i<<" hwnd="<<bar.hwnd<<" primary="<<bar.primary<<" identityMatches="<<sameBar(bar)
   <<" visible="<<(IsWindowVisible(window)!=FALSE)<<" ownerPid="<<owner<<" ownerThread="<<thread
   <<" style="<<static_cast<std::uint64_t>(GetWindowLongPtrW(window,GWL_STYLE))
   <<" rectObserved="<<rectObserved<<" rect="<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom<<"]";
 }
 std::cerr<<std::endl;
 std::vector<PROCESSENTRY32W> entries;const bool enumerated=processEntries(entries);const auto enumerationError=enumerated?ERROR_SUCCESS:GetLastError();DWORD count=0;
 const auto expectedImage=executablePath().parent_path()/L"AetherWatchdog.exe";
 if(enumerated)for(const auto& entry:entries)if(entry.th32ParentProcessID==test.worker.pid&&_wcsicmp(entry.szExeFile,L"AetherWatchdog.exe")==0){
  ++count;Handle helper(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID));const auto openError=helper?ERROR_SUCCESS:GetLastError();
  const auto actualImage=helper?imageOf(helper.value):std::wstring{};
  std::cerr<<"DIAG synthetic session child: stage="<<stage<<"; pid="<<entry.th32ProcessID
   <<"; birth="<<(helper?birthOf(helper.value):0)<<"; wait="<<(helper?WaitForSingleObject(helper.value,0):WAIT_FAILED)
   <<"; expectedImage="<<(!actualImage.empty()&&_wcsicmp(actualImage.c_str(),expectedImage.c_str())==0)
   <<"; openError="<<openError<<std::endl;
 }
 std::cerr<<"DIAG synthetic session child enumeration: stage="<<stage<<"; complete="<<enumerated<<"; error="<<enumerationError<<"; matchingChildren="<<count<<std::endl;
}
void appSyntheticSessionCase(){
 SafeCase test;if(!appAdmission(test,L"synthetic-session","owned synthetic session messages"))return;
 DWORD session=0;ProcessIdToSessionId(test.worker.pid,&session);auto firstHelper=test.helper();DWORD_PTR reply=0;
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_SESSION_LOCK,session),"synthetic session: LOCK delivered only to the owned controller");
 check(waitFor([&]{return barsRestored(*test.state);},8000),"synthetic session: LOCK restores native visibility and complete appbar state");
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_SESSION_LOCK,session)&&!unpublishedHelper(test.worker),
  "synthetic session: duplicate LOCK creates no helper generation");
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_CONSOLE_DISCONNECT,session),"synthetic session: DISCONNECT delivered only to the owned controller");
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_CONSOLE_CONNECT,session),"synthetic session: CONNECT delivered only to the owned controller");
 check(barsRestored(*test.state)&&!unpublishedHelper(test.worker),"synthetic session: CONNECT while still locked remains paused with native taskbars restored");
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_SESSION_UNLOCK,session),"synthetic session: UNLOCK delivered only to the owned controller");
 check(waitFor([&]{return barsHidden(*test.state);},8000)&&test.observeAppHelper(),"synthetic session: UNLOCK intentionally restores the preferred replacement mode");
 const auto resumedHelper=InterlockedCompareExchange(&test.state->helperPid,0,0);
 const auto resumedBirth=InterlockedCompareExchange64(&test.state->helperBirth,0,0);
 check(test.command(WM_WTSSESSION_CHANGE,&reply,8000,WTS_SESSION_UNLOCK,session)&&test.observeAppHelper()
  &&InterlockedCompareExchange(&test.state->helperPid,0,0)==resumedHelper&&InterlockedCompareExchange64(&test.state->helperBirth,0,0)==resumedBirth,
  "synthetic session: duplicate UNLOCK preserves the same acknowledged helper generation");
 auto resumedOwnedHelper=test.helper();const auto sessionTraceStarted=GetTickCount64();
 // Retain cheap timestamps/replies, and evaluate the original immediate
 // predicates back-to-back. Detailed Shell queries must not run before or
 // between these commands because they can pump Explorer's pending work.
 reply=0;const auto queryStarted=GetTickCount64();const bool queryDelivered=test.command(WM_QUERYENDSESSION,&reply,8000);
 const auto queryReply=reply;const bool queryProtected=queryDelivered&&queryReply==TRUE&&barsHidden(*test.state);
 const auto queryElapsed=GetTickCount64()-queryStarted;
 reply=0;const auto cancelStarted=GetTickCount64();const bool cancelDelivered=test.command(WM_ENDSESSION,&reply,8000,FALSE,0);
 const auto cancelReply=reply;const bool cancelProtected=cancelDelivered&&barsHidden(*test.state);
 const auto cancelElapsed=GetTickCount64()-cancelStarted;
 std::cerr<<"DIAG synthetic session reply: message=QUERYENDSESSION; delivered="<<queryDelivered<<"; reply="<<queryReply
  <<"; elapsedMs="<<queryElapsed<<"; immediateProtected="<<queryProtected<<std::endl;
 std::cerr<<"DIAG synthetic session reply: message=ENDSESSION-FALSE; delivered="<<cancelDelivered<<"; reply="<<cancelReply
  <<"; elapsedMs="<<cancelElapsed<<"; immediateProtected="<<cancelProtected<<std::endl;
 check(queryDelivered,"synthetic session: QUERYENDSESSION delivered only to the owned controller");
 check(queryDelivered&&queryReply==TRUE,"synthetic session: QUERYENDSESSION returns TRUE");
 check(queryProtected,"synthetic session: QUERYENDSESSION acknowledges while active hiding remains protected");
 check(cancelDelivered,"synthetic session: canceled ENDSESSION delivered only to the owned controller");
 check(cancelProtected,"synthetic session: canceled ENDSESSION does not revoke active replacement");
 describeSyntheticSession(test,resumedOwnedHelper,"after-both-immediate-predicates",sessionTraceStarted);
  // Diagnostic observations only. The original immediate acceptance booleans
 // above are never replaced with eventual results from this bounded trace.
 const auto traceStarted=GetTickCount64();
 for(unsigned sample=0;sample<5&&GetTickCount64()-traceStarted<600;++sample){
  Sleep(100);describeSyntheticSession(test,resumedOwnedHelper,"post-command-trace",sessionTraceStarted);
 }
  check(firstHelper&&WaitForSingleObject(firstHelper.value,0)==WAIT_OBJECT_0,"synthetic session: original helper retired during LOCK");
 test.finish("owned synthetic session messages");
}
 bool pathBelow(const std::filesystem::path& candidate,const std::filesystem::path& root){
 auto child=candidate.begin(),parent=root.begin();
 for(;parent!=root.end();++parent,++child)if(child==candidate.end()||_wcsicmp(child->native().c_str(),parent->native().c_str())!=0)return false;
 return child!=candidate.end();
}
bool createGuardianDirectory(const wchar_t* argument,std::filesystem::path& output){
 std::error_code error;const std::filesystem::path requested(argument);
 if(!requested.is_absolute())return false;
 auto staged=std::filesystem::weakly_canonical(executablePath().parent_path().parent_path().parent_path(),error);
 if(error||_wcsicmp(staged.filename().c_str(),L"AetherUI")!=0||_wcsicmp(staged.parent_path().filename().c_str(),L"AetherUI-v03")!=0)return false;
 if(!std::filesystem::is_regular_file(staged/L"CODEX_HANDOFF.md",error)||error)return false;
 output=std::filesystem::weakly_canonical(requested,error);
 if(error||!pathBelow(output,staged)||std::filesystem::exists(output,error)||error)return false;
 // The caller must choose a fresh leaf under an existing ordinary parent.
 // Reject junctions/reparse points through the entire staged parent chain.
 auto cursor=output.parent_path();
 while(true){
  const auto attributes=GetFileAttributesW(cursor.c_str());
  if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
  if(_wcsicmp(cursor.native().c_str(),staged.native().c_str())==0)break;
  if(!pathBelow(cursor,staged))return false;cursor=cursor.parent_path();
 }
 return std::filesystem::create_directory(output,error)&&!error;
}
bool writeGuardianJson(const std::filesystem::path& directory,const wchar_t* name,const std::string& body){
 if(body.size()>8192)return false;
 const auto target=directory/name,temporary=directory/(std::wstring(name)+L".tmp");
 Handle file(CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr));
 if(!file)return false;DWORD written=0;
 bool success=WriteFile(file.value,body.data(),static_cast<DWORD>(body.size()),&written,nullptr)&&written==body.size()&&FlushFileBuffers(file.value);
 file.reset();return success&&MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH);
}
ULONGLONG utcFileTime(){
 FILETIME now{};GetSystemTimeAsFileTime(&now);return (static_cast<ULONGLONG>(now.dwHighDateTime)<<32)|now.dwLowDateTime;
}
std::string jsonBoolean(bool value){return value?"true":"false";}
struct OwnedPanelObservation {DWORD topBars{},docks{},visibleTopBars{},visibleDocks{},widgets{},visibleWidgets{};bool complete=false;};
OwnedPanelObservation observeOwnedPanels(const Child& app){
 if(!app.alive()||birthOf(app.process.value)!=app.birth)return {};
 struct Search {DWORD pid;OwnedPanelObservation panels;} search{app.pid,{}};
 const bool enumerated=EnumWindows([](HWND window,LPARAM value)->BOOL{
  auto& search=*reinterpret_cast<Search*>(value);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
  if(pid!=search.pid)return TRUE;wchar_t title[256]{};GetWindowTextW(window,title,256);
  if(wcscmp(title,L"Aether Top Bar")==0){++search.panels.topBars;if(IsWindowVisible(window))++search.panels.visibleTopBars;}
  else if(wcscmp(title,L"Aether Dock")==0){++search.panels.docks;if(IsWindowVisible(window))++search.panels.visibleDocks;}
  else if(std::wstring_view(title).starts_with(L"Aether extension:")){++search.panels.widgets;if(IsWindowVisible(window))++search.panels.visibleWidgets;}
  return TRUE;
 },reinterpret_cast<LPARAM>(&search))!=FALSE;
 search.panels.complete=enumerated&&app.alive()&&birthOf(app.process.value)==app.birth;
 return search.panels;
}
int appMeasurementGuardian(const wchar_t* argument,int sleekMode=-1,bool soak=false){
 if(!TaskbarReplacementValidated){std::cerr<<"Production gate disabled; native acceptance and review are required first.\n";return 3;}
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before resource observation.\n";return 2;}
 std::filesystem::path directory;
 if(!createGuardianDirectory(argument,directory)){std::cerr<<"Choose a fresh absolute artifact leaf under the staged project with ordinary existing parent directories.\n";return 4;}
 const DWORD guardianLifetime=soak?1800000:300000;
 const auto startedTick=GetTickCount64(),startedUtc=utcFileTime();SafeCase test(guardianLifetime);
 const bool startup=appAdmission(test,L"resource-observation","actual active-mode resource guardian",false,sleekMode);
 const auto readyTick=GetTickCount64(),readyUtc=utcFileTime();
 auto helper=startup?test.helper():Handle{};
 // Snapshot owned panel exposure before Ready and before resource observers start.
 const auto panels=startup?observeOwnedPanels(test.worker):OwnedPanelObservation{};
 const bool matchedPanels=startup&&panels.complete&&panels.topBars==test.state->monitorCount&&panels.docks==test.state->monitorCount
  &&panels.visibleTopBars==test.state->monitorCount&&panels.visibleDocks==test.state->monitorCount;
 const DWORD expectedWidgets=sleekMode==1?10:0;
 const bool ready=startup&&matchedPanels&&panels.widgets==expectedWidgets&&panels.visibleWidgets==expectedWidgets&&static_cast<bool>(helper)&&test.worker.alive()&&test.sentinelAlive()&&barsHidden(*test.state);
 bool readyWritten=false,requested=false,timedOut=false,unexpectedExit=false;
 if(ready){
  const std::string body="{\"Ready\":true,\"OwnedAppPid\":"+std::to_string(test.worker.pid)
   +",\"ProcessBirth\":\""+std::to_string(test.worker.birth)+"\",\"HelperPid\":"+std::to_string(GetProcessId(helper.value))
   +",\"HelperBirth\":\""+std::to_string(birthOf(helper.value))+"\",\"SentinelPid\":"+std::to_string(test.sentinelPid())
   +",\"SentinelOutsideJob\":true,\"NativeBarsHidden\":true,\"TaskbarCount\":"+std::to_string(test.state->barCount)
   +",\"MonitorCount\":"+std::to_string(test.state->monitorCount)+",\"SentinelLifetimeMilliseconds\":"+std::to_string(guardianLifetime)
   +",\"SleekMode\":"+std::to_string(sleekMode)+",\"ExpectedWidgetCount\":"+std::to_string(expectedWidgets)+",\"VisibleWidgetCount\":"+std::to_string(panels.visibleWidgets)
   +",\"FullscreenSuppression\":false,\"ConfiguredTopBarPerDisplay\":true,\"ConfiguredDockPerDisplay\":true"
   +",\"ExpectedTopBarCount\":"+std::to_string(test.state->monitorCount)+",\"ExpectedDockCount\":"+std::to_string(test.state->monitorCount)
   +",\"OwnedTopBarCount\":"+std::to_string(panels.topBars)+",\"OwnedDockCount\":"+std::to_string(panels.docks)
   +",\"VisibleTopBarCount\":"+std::to_string(panels.visibleTopBars)+",\"VisibleDockCount\":"+std::to_string(panels.visibleDocks)
   +",\"PanelObservationComplete\":true,\"PanelExposureMatched\":true"
   +",\"StartedTickMilliseconds\":"+std::to_string(startedTick)+",\"ReadyTickMilliseconds\":"+std::to_string(readyTick)
   +",\"StartedUtcFileTime\":\""+std::to_string(startedUtc)+"\",\"ReadyUtcFileTime\":\""+std::to_string(readyUtc)+"\"}\n";
  readyWritten=writeGuardianJson(directory,L"ready.json",body);
 }
 unsigned hiddenSamples=0,hiddenFailures=0;ULONGLONG firstHiddenFailureTick=0;
 if(readyWritten){
  const auto deadline=GetTickCount64()+(soak?1500000:180000);
  HANDLE lifetime[]{test.worker.process.value,helper.value};
  while(GetTickCount64()<deadline){
   const auto attributes=GetFileAttributesW((directory/L"finish.json").c_str());
   if(attributes!=INVALID_FILE_ATTRIBUTES){
    if(!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))requested=true;
    break;
   }
   if(WaitForMultipleObjects(2,lifetime,FALSE,250)!=WAIT_TIMEOUT){unexpectedExit=true;break;}
   if(!test.sentinelAlive()){unexpectedExit=true;break;}
   if(soak){++hiddenSamples;if(!barsHidden(*test.state)){++hiddenFailures;firstHiddenFailureTick=GetTickCount64();unexpectedExit=true;break;}}
  }
  timedOut=!requested&&!unexpectedExit;
 }
 // The observers have finished before this additional Shell read. Sampling
 // itself still performs no native scans.
 const bool nativeBarsHiddenAtFinish=requested&&test.worker.alive()&&helper&&WaitForSingleObject(helper.value,0)==WAIT_TIMEOUT&&barsHidden(*test.state);
 const bool cleanup=test.state&&test.finish("actual active-mode resource guardian");
 const auto finishedTick=GetTickCount64(),finishedUtc=utcFileTime();
 const bool restored=test.state&&barsRestored(*test.state),workAreas=test.state&&workAreasRestored(*test.state);
 const bool noIntervention=test.state&&InterlockedCompareExchange(&test.state->sentinelTriggered,0,0)==0;
 const bool appStopped=!test.worker.alive(),helperStopped=helper&&WaitForSingleObject(helper.value,0)==WAIT_OBJECT_0;
 DWORD exit=STILL_ACTIVE;if(test.worker.process&&!test.worker.alive())GetExitCodeProcess(test.worker.process.value,&exit);
 const bool success=readyWritten&&requested&&nativeBarsHiddenAtFinish&&!timedOut&&!unexpectedExit&&cleanup&&restored&&workAreas&&noIntervention&&appStopped&&helperStopped&&exit==0&&failures==0&&hiddenFailures==0;
 const std::string body="{\"Success\":"+jsonBoolean(success)+",\"ReadyPublished\":"+jsonBoolean(readyWritten)
  +",\"NativeBarsHiddenAtFinish\":"+jsonBoolean(nativeBarsHiddenAtFinish)
  +",\"FullscreenSuppression\":false,\"PanelExposureMatchedAtReady\":"+jsonBoolean(matchedPanels)
  +",\"FinishRequested\":"+jsonBoolean(requested)+",\"TimedOut\":"+jsonBoolean(timedOut)+",\"UnexpectedExit\":"+jsonBoolean(unexpectedExit)
  +",\"HiddenSamples\":"+std::to_string(hiddenSamples)+",\"HiddenFailures\":"+std::to_string(hiddenFailures)+",\"FirstHiddenFailureTick\":"+std::to_string(firstHiddenFailureTick)
  +",\"TaskbarsRestored\":"+jsonBoolean(restored)+",\"CompleteTaskbarStateRestored\":"+jsonBoolean(restored)
  +",\"PhysicalWorkAreasRestored\":"+jsonBoolean(workAreas)+",\"NoFailsafeIntervention\":"+jsonBoolean(noIntervention)
  +",\"OwnedAppStopped\":"+jsonBoolean(appStopped)+",\"OwnedWatchdogStopped\":"+jsonBoolean(helperStopped)
  +",\"SentinelStopped\":"+jsonBoolean(!test.sentinelAlive())+",\"OwnedAppExitCode\":"+std::to_string(exit)
  +",\"OwnedAppPid\":"+std::to_string(test.worker.pid)+",\"ProcessBirth\":\""+std::to_string(test.worker.birth)
  +"\",\"FinishedTickMilliseconds\":"+std::to_string(finishedTick)+",\"FinishedUtcFileTime\":\""+std::to_string(finishedUtc)+"\"}\n";
 const bool written=writeGuardianJson(directory,L"cleanup.json",body);
 return success&&written?0:1;
}
bool noLiveAppWatchdog(const Child& app){
 if(!app.alive())return false;std::vector<PROCESSENTRY32W> entries;if(!processEntries(entries))return false;
 for(const auto& entry:entries)if(entry.th32ParentProcessID==app.pid&&_wcsicmp(entry.szExeFile,L"AetherWatchdog.exe")==0){
  Handle helper(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID));
  if(!helper){if(GetLastError()==ERROR_INVALID_PARAMETER)continue;return false;}
  const auto status=WaitForSingleObject(helper.value,0);if(status!=WAIT_OBJECT_0)return false;
 }
 return true;
}
int appbarOnlyAcceptance(){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before Appbar-only acceptance.\n";return 2;}
 SafeCase test;
 if(!check(test.ready(),"actual Appbar-only: immutable before-state and independent sentinel acknowledged"))return 1;
 if(!check(test.launchApp(L"appbar-only",false),"actual Appbar-only: private-profile App with hideTaskbar=false reaches Ready"))return 1;
 check(noLiveAppWatchdog(test.worker),"actual Appbar-only: complete child observation finds no live native-hiding watchdog");
 check(barsRestored(*test.state),"actual Appbar-only: exact native visibility and original raw state stay unchanged while reservations are active");
 DWORD_PTR reply=0;check(test.command(WM_NULL,&reply,500),"actual Appbar-only: owned controller remains responsive");
 Handle noHelper;const auto started=GetTickCount64();
 describeSyntheticSession(test,noHelper,"appbar-only-before-exit",started);
 describeWorkAreas(*test.state,"appbar-only-before-exit");
 test.finish("actual Appbar-only normal exit");
 describeSyntheticSession(test,noHelper,"appbar-only-after-exit",started);
 describeWorkAreas(*test.state,"appbar-only-after-exit");
 std::cout<<checks-failures<<"/"<<checks<<" separate Appbar-only cleanup checks passed."<<std::endl;return failures?1:0;
}
void appAutoHideStartupCase(){
 SafeCase test;
 if(!check(test.ready(),"actual App auto-hide: immutable before-state and independent outside-job sentinel acknowledged"))return;
 if(!check(test.launchApp(L"auto-hide-startup",true,true,true),"actual App auto-hide: private hideTaskbar=true and autoHide=true profile reaches Ready"))return;
 // This predicate is immediate at Ready, before helper discovery or any wait.
 // An eventual hide cannot replace this startup observation.
 check(barsHidden(*test.state),"actual App auto-hide: every genuine native taskbar is hidden immediately at Ready");
 if(!check(test.observeAppHelper(),"actual App auto-hide: actual watchdog PID, image and birth verified"))return;
 auto helper=test.helper();const auto originalHelperBirth=InterlockedCompareExchange64(&test.state->helperBirth,0,0);
 const auto originalHelperPid=InterlockedCompareExchange(&test.state->helperPid,0,0);
 const auto started=GetTickCount64();
 for(unsigned sample=0;sample<10;++sample){
  if(sample)Sleep(100);
  const bool active=test.worker.alive()&&birthOf(test.worker.process.value)==test.worker.birth
   &&helper&&WaitForSingleObject(helper.value,0)==WAIT_TIMEOUT
   &&InterlockedCompareExchange(&test.state->helperPid,0,0)==originalHelperPid
   &&InterlockedCompareExchange64(&test.state->helperBirth,0,0)==originalHelperBirth;
  const bool hidden=barsHidden(*test.state),protectedFromPresentation=barsHidden(*test.state,false);
  std::cout<<"DIAG auto-hide startup sample="<<sample<<"; elapsedMs="<<GetTickCount64()-started
   <<"; sameOwnedGeneration="<<active<<"; nativeHidden="<<hidden<<"; zeroAlphaProtected="<<protectedFromPresentation<<std::endl;
  // Explorer may issue SHOW during reservation/style callbacks. Require the
  // independently queried zero-alpha guard on every sample, including SHOW;
  // the strict logical-hidden check remains required after settlement below.
  check(active&&protectedFromPresentation,("actual App auto-hide: independently zero-alpha protected sample "+std::to_string(sample)).c_str());
 }
 check(waitFor([&]{return barsHidden(*test.state);},2500),"actual App auto-hide: strict logical hiding restored after startup callbacks");
 DWORD_PTR reply=0;check(test.command(WM_NULL,&reply,500),"actual App auto-hide: controller remains responsive with the dock auto-hiding");
 test.finish("actual App auto-hide normal exit");
}
int appAutoHideAcceptance(){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before auto-hide startup acceptance.\n";return 2;}
 if(!TaskbarReplacementValidated){std::cerr<<"Production startup gate disabled; run guard acceptance/review first.\n";return 3;}
 appAutoHideStartupCase();
 std::cout<<checks-failures<<"/"<<checks<<" separate actual Aether auto-hide startup checks passed."<<std::endl;return failures?1:0;
}
 int appAcceptance(){
 if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller")){std::cerr<<"Close Aether normally before startup acceptance.\n";return 2;}
 if(!TaskbarReplacementValidated){std::cerr<<"Production startup gate disabled; run guard acceptance/review first.\n";return 3;}
 appCase(L"normal",false,false);if(failures)return 1;appCase(L"emergency",true,false);if(failures)return 1;appCase(L"crash",false,true);if(failures)return 1;appHelperCrashCase();if(failures)return 1;appControllerHangCase();if(failures)return 1;appSyntheticSessionCase();if(failures)return 1;appAutoHideStartupCase();
 std::cout<<checks-failures<<"/"<<checks<<" actual Aether startup taskbar checks passed."<<std::endl;return failures?1:0;
}
}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc>=2&&std::wstring_view(argv[1])==L"--sentinel")return sentinel(argc,argv);
 if(argc>=2&&std::wstring_view(argv[1])==L"--guard-fixture")return fixture(argc,argv);
 if(argc==2&&std::wstring_view(argv[1])==L"--native-acceptance")return guardAcceptance();
 if(argc==2&&std::wstring_view(argv[1])==L"--native-normal-disable-only")return isolatedGuardAcceptance(false);
 if(argc==2&&std::wstring_view(argv[1])==L"--native-normal-disable-hold-only")return isolatedGuardAcceptance(false,true);
 if(argc==2&&std::wstring_view(argv[1])==L"--native-calibration-only")return isolatedGuardAcceptance(true);
 if(argc==2&&std::wstring_view(argv[1])==L"--native-helper-crash-only")return isolatedHelperCrashAcceptance();
 if(argc==2&&std::wstring_view(argv[1])==L"--native-helper-crash-hold-only")return isolatedHelperCrashAcceptance(true);
 if(argc==2&&std::wstring_view(argv[1])==L"--app-startup-acceptance")return appAcceptance();
 if(argc==2&&std::wstring_view(argv[1])==L"--app-effects-acceptance"){if(!TaskbarReplacementValidated)return 3;if(FindWindowW(L"Aether.Controller.v3",L"Aether UI Controller"))return 2;appCase(L"effects-normal",false,false,2);if(!failures)appCase(L"effects-emergency",true,false,2);if(!failures)appCase(L"effects-crash",false,true,2);std::cout<<checks-failures<<"/"<<checks<<" effects startup/recovery checks passed.\n";return failures?1:0;}
 if(argc==2&&std::wstring_view(argv[1])==L"--app-auto-hide-startup-acceptance")return appAutoHideAcceptance();
 if(argc==2&&std::wstring_view(argv[1])==L"--appbar-only-acceptance")return appbarOnlyAcceptance();
 if(argc==3&&std::wstring_view(argv[1])==L"--app-measurement-guardian")return appMeasurementGuardian(argv[2]);
 if(argc==4&&std::wstring_view(argv[1])==L"--app-sleek-measurement-guardian"&&(std::wstring_view(argv[2])==L"plain"||std::wstring_view(argv[2])==L"full"||std::wstring_view(argv[2])==L"effects"||std::wstring_view(argv[2])==L"glass"))return appMeasurementGuardian(argv[3],std::wstring_view(argv[2])==L"effects"?2:std::wstring_view(argv[2])==L"glass"?3:std::wstring_view(argv[2])==L"full"?1:0);
 if(argc==3&&std::wstring_view(argv[1])==L"--app-sleek-soak-guardian")return appMeasurementGuardian(argv[2],1,true);
 std::cout<<"Opt-in only: --native-acceptance, --native-normal-disable-only, --native-normal-disable-hold-only, --native-calibration-only, --native-helper-crash-only, --native-helper-crash-hold-only, --app-startup-acceptance, --app-auto-hide-startup-acceptance, --appbar-only-acceptance, or --app-measurement-guardian <fresh absolute staged artifact directory>.\n"
  "Never restarts Explorer, changes system files, moves user windows or enables startup.\n";return 2;
}
