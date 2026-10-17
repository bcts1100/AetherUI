#include "desktop.hpp"
#include <shellscalingapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
namespace aether::win {
std::vector<Monitor> monitors(){std::vector<Monitor> out;EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM p)->BOOL{auto& list=*reinterpret_cast<std::vector<Monitor>*>(p);MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);if(GetMonitorInfoW(h,reinterpret_cast<MONITORINFO*>(&mi))){UINT x=96,y=96;GetDpiForMonitor(h,MDT_EFFECTIVE_DPI,&x,&y);list.push_back({h,mi.szDevice,rect(mi.rcMonitor),rect(mi.rcWork),static_cast<int>(x),(mi.dwFlags&MONITORINFOF_PRIMARY)!=0});}return TRUE;},reinterpret_cast<LPARAM>(&out));std::stable_sort(out.begin(),out.end(),[](const Monitor& a,const Monitor& b){if(a.primary!=b.primary)return a.primary;return a.name<b.name;});return out;}
struct WindowCollector::Impl {
 struct Process {Handle handle;std::wstring path;std::uint64_t generation=0,birth=0;};
 struct Identity {DWORD pid=0;std::uint64_t generation=0,epoch=0;std::wstring value;bool desktopKnown=false,onCurrentDesktop=true,needs=true,queued=false;};
 const std::wstring explorerImage=[](){
  wchar_t directory[32768]{};const UINT length=GetWindowsDirectoryW(directory,32768);
  return length&&length<32768?(std::filesystem::path(directory)/L"explorer.exe").wstring():std::wstring{};
 }();
 std::unordered_map<DWORD,Process> processes;std::unordered_map<HWND,Identity> identities;std::uint64_t generation=0,epoch=0;
};
namespace {
std::uint64_t processBirth(HANDLE handle){FILETIME created{},exited{},kernel{},user{};if(!handle||!GetProcessTimes(handle,&created,&exited,&kernel,&user))return 0;return (static_cast<std::uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime;}
}
WindowCollector::WindowCollector():impl_(std::make_unique<Impl>()){}
WindowCollector::~WindowCollector()=default;
void WindowCollector::invalidateIdentity(HWND hwnd){auto found=impl_->identities.find(hwnd);if(found!=impl_->identities.end()){found->second.epoch=++impl_->epoch;found->second.needs=true;found->second.queued=false;}}
void WindowCollector::forgetWindow(HWND hwnd){impl_->identities.erase(hwnd);/* impl_->epoch is global and deliberately retained across erasure. */}
std::vector<Window> windows(){WindowCollector collector;return collector.snapshot();}
std::vector<WindowMetadataRequest> WindowCollector::takeMetadataRequests(std::size_t maximum){
 maximum=std::min<std::size_t>(maximum,128);std::vector<WindowMetadataRequest> result;
 for(auto& [hwnd,id]:impl_->identities){if(result.size()>=maximum)break;if(!id.needs||id.queued)continue;auto process=impl_->processes.find(id.pid);if(process==impl_->processes.end()||process->second.generation!=id.generation||!process->second.birth||!process->second.handle||WaitForSingleObject(process->second.handle.value,0)!=WAIT_TIMEOUT||!windowAlive(hwnd,id.pid))continue;
  result.push_back({reinterpret_cast<std::uintptr_t>(hwnd),process->second.birth,id.epoch,id.pid});id.queued=true;++stats_.identityQueries;
 }return result;
}
void WindowCollector::rejectMetadata(const std::vector<WindowMetadataRequest>& requests){for(const auto& token:requests){auto found=impl_->identities.find(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(token.hwnd)));if(found!=impl_->identities.end()&&found->second.pid==token.pid&&found->second.epoch==token.epoch)found->second.queued=false;}}
bool WindowCollector::acceptMetadata(const std::vector<WindowMetadata>& values){
 bool changed=false;for(const auto& value:values){const auto& token=value.token;HWND hwnd=reinterpret_cast<HWND>(static_cast<std::uintptr_t>(token.hwnd));auto identity=impl_->identities.find(hwnd);auto process=impl_->processes.find(token.pid);
  if(identity==impl_->identities.end()||process==impl_->processes.end()||!identity->second.queued||identity->second.pid!=token.pid||identity->second.epoch!=token.epoch||identity->second.generation!=process->second.generation||process->second.birth!=token.birth||!process->second.handle||WaitForSingleObject(process->second.handle.value,0)!=WAIT_TIMEOUT||!windowAlive(hwnd,token.pid))continue;
  auto& id=identity->second;id.queued=false;id.needs=false;
  if(!value.identity.empty()&&value.identity!=id.value){id.value=value.identity;changed=true;}
  if(value.desktopKnown){changed=changed||!id.desktopKnown||id.onCurrentDesktop!=value.onCurrentDesktop;id.desktopKnown=true;id.onCurrentDesktop=value.onCurrentDesktop;}
 }return changed;
}
std::vector<Window> WindowCollector::snapshot(bool reconcile){
 auto started=std::chrono::steady_clock::now();++stats_.snapshots;
 if(reconcile)for(auto& [hwnd,id]:impl_->identities){(void)hwnd;id.epoch=++impl_->epoch;id.needs=true;id.queued=false;}
 std::vector<Window> out;std::unordered_set<DWORD> seenProcesses;std::unordered_set<HWND> seenWindows;
 struct Context {WindowCollector* self;std::vector<Window>* out;std::unordered_set<DWORD>* processes;std::unordered_set<HWND>* windows;};Context ctx{this,&out,&seenProcesses,&seenWindows};
 EnumWindows([](HWND h,LPARAM param)->BOOL{
  auto& c=*reinterpret_cast<Context*>(param);auto& impl=*c.self->impl_;auto& stats=c.self->stats_;
  if(c.out->size()>=2048||c.windows->size()>=2048)return FALSE;
  if(!IsWindowVisible(h))return TRUE;DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(!pid||pid==GetCurrentProcessId())return TRUE;
  auto ex=GetWindowLongPtrW(h,GWL_EXSTYLE);if((ex&WS_EX_TOOLWINDOW)||(ex&WS_EX_NOACTIVATE))return TRUE;
  if(GetWindow(h,GW_OWNER)&&!(ex&WS_EX_APPWINDOW))return TRUE;BOOL cloaked=FALSE;DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));if(cloaked)return TRUE;
  wchar_t cls[128]{};GetClassNameW(h,cls,128);if(wcscmp(cls,L"Shell_TrayWnd")==0||wcscmp(cls,L"Shell_SecondaryTrayWnd")==0||wcscmp(cls,L"Progman")==0||wcscmp(cls,L"WorkerW")==0)return TRUE;
  auto title=windowText(h);if(title.empty())return TRUE;auto process=impl.processes.find(pid);
  bool alive=process!=impl.processes.end()&&process->second.handle&&WaitForSingleObject(process->second.handle.value,0)==WAIT_TIMEOUT;
  if(!alive){Impl::Process next;next.handle=Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));next.generation=++impl.generation;next.birth=processBirth(next.handle.value);++stats.pathQueries;
   if(next.handle){std::wstring path(32768,L'\0');DWORD count=static_cast<DWORD>(path.size());if(QueryFullProcessImageNameW(next.handle.value,0,path.data(),&count)){path.resize(count);next.path=std::move(path);}}
   process=impl.processes.insert_or_assign(pid,std::move(next)).first;
  }else ++stats.pathCacheHits;
  c.processes->insert(pid);
  // Explorer's input-occlusion surface is infrastructure, not a fullscreen app.
  // Keep its process cached, and leave same-class windows from other owners eligible.
  const auto& owner=process->second;
  if(wcscmp(cls,L"LockScreenInputOcclusionFrame")==0&&!impl.explorerImage.empty()
   &&owner.handle&&owner.birth&&GetProcessId(owner.handle.value)==pid
   &&WaitForSingleObject(owner.handle.value,0)==WAIT_TIMEOUT&&processBirth(owner.handle.value)==owner.birth
   &&_wcsicmp(owner.path.c_str(),impl.explorerImage.c_str())==0&&windowAlive(h,pid)){
   wchar_t currentClass[128]{};
   if(GetClassNameW(h,currentClass,128)&&wcscmp(currentClass,L"LockScreenInputOcclusionFrame")==0)return TRUE;
  }
  auto path=process->second.path;auto generation=process->second.generation;auto identity=impl.identities.find(h);
  if(identity==impl.identities.end()||identity->second.pid!=pid||identity->second.generation!=generation){
   std::wstring id=lower(path);if(id.empty())id=L"pid:"+std::to_wstring(pid);Impl::Identity next;next.pid=pid;next.generation=generation;next.epoch=++impl.epoch;next.value=std::move(id);identity=impl.identities.insert_or_assign(h,std::move(next)).first;
  }else ++stats.identityCacheHits;
  c.windows->insert(h);if(identity->second.desktopKnown&&!identity->second.onCurrentDesktop)return TRUE;
  auto monitor=MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST);MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&mi));
  RECT bounds{};if(FAILED(DwmGetWindowAttribute(h,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))))GetWindowRect(h,&bounds);
  const bool decoratedMaximized=IsZoomed(h)!=FALSE&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_CAPTION)==WS_CAPTION;
  bool minimized=IsIconic(h)!=FALSE;bool full=!minimized&&!decoratedMaximized&&bounds.left<=mi.rcMonitor.left&&bounds.top<=mi.rcMonitor.top&&bounds.right>=mi.rcMonitor.right&&bounds.bottom>=mi.rcMonitor.bottom;
  c.out->push_back({h,pid,std::move(title),std::move(path),identity->second.value,mi.szDevice,h==GetForegroundWindow(),full,rect(bounds),minimized,process->second.birth});return TRUE;
 },reinterpret_cast<LPARAM>(&ctx));
 std::erase_if(impl_->processes,[&](const auto& entry){return !seenProcesses.contains(entry.first);});std::erase_if(impl_->identities,[&](const auto& entry){return !seenWindows.contains(entry.first);});
 stats_.totalMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return out;
}void activate(const Window& w,bool minimizeActive){if(!windowAlive(w.hwnd,w.pid))return;if(minimizeActive&&GetForegroundWindow()==w.hwnd){ShowWindowAsync(w.hwnd,SW_MINIMIZE);return;}if(IsIconic(w.hwnd))ShowWindowAsync(w.hwnd,SW_RESTORE);if(!SetForegroundWindow(w.hwnd)){FLASHWINFO f{sizeof(f),w.hwnd,FLASHW_TRAY,2,0};FlashWindowEx(&f);}}
void snapActive(int zone){HWND h=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(!h||pid==GetCurrentProcessId())return;MONITORINFO mi{sizeof(mi)};if(!GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&mi))return;auto r=snapRect(rect(mi.rcWork),zone,8);if(IsZoomed(h)||IsIconic(h))ShowWindow(h,SW_RESTORE);SetWindowPos(h,nullptr,r.left,r.top,r.width(),r.height(),SWP_NOZORDER|SWP_NOACTIVATE);}
HWND WindowEvents::receiver_=nullptr;
RefreshGate WindowEvents::gate_{};
std::set<HWND> WindowEvents::tracked_{};
std::set<HWND> WindowEvents::invalidated_{};
std::set<HWND> WindowEvents::moving_{};
std::set<HWND> WindowEvents::moveEnds_{};
std::set<HWND> WindowEvents::lifecycle_{};
std::vector<HWND> WindowEvents::takeLifecycleChanges(){std::vector<HWND> result(lifecycle_.begin(),lifecycle_.end());lifecycle_.clear();return result;}
std::vector<HWND> WindowEvents::takeUserMoveEnds(){std::vector<HWND> result(moveEnds_.begin(),moveEnds_.end());moveEnds_.clear();return result;}
std::vector<HWND> WindowEvents::takeIdentityInvalidations(){
 std::vector<HWND> result(invalidated_.begin(),invalidated_.end());invalidated_.clear();return result;
}
void CALLBACK WindowEvents::callback(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG child,DWORD,DWORD){
 if(!receiver_||gate_.paused)return;
 // Shell desktop notifications can be omitted while Windows switches views.
 // Reconcile once for a foreground/cloak burst, even if the window snapshot
 // itself is unchanged. The controller acknowledges this separate queue bit.
 if(event==EVENT_SYSTEM_DESKTOPSWITCH||event==EVENT_SYSTEM_FOREGROUND||event==EVENT_OBJECT_CLOAKED||event==EVENT_OBJECT_UNCLOAKED){
  constexpr auto pending=L"Aether.Workspace.RefreshQueued";
  if(!GetPropW(receiver_,pending)&&SetPropW(receiver_,pending,reinterpret_cast<HANDLE>(1))){if(!PostMessageW(receiver_,WM_APP+44,0,0))RemovePropW(receiver_,pending);}
 }
 if(!h)return;
 if(event>=EVENT_OBJECT_CREATE&&(object!=OBJID_WINDOW||child!=0))return;
 if(event==EVENT_SYSTEM_MOVESIZESTART)moving_.insert(h);
 if(event==EVENT_SYSTEM_MOVESIZEEND){moving_.erase(h);moveEnds_.insert(h);}
 if(event==EVENT_OBJECT_DESTROY){moving_.erase(h);moveEnds_.erase(h);}
 if(event==EVENT_OBJECT_CREATE||event==EVENT_OBJECT_DESTROY)lifecycle_.insert(h);
 if(event==EVENT_OBJECT_LOCATIONCHANGE&&h!=GetForegroundWindow())return;
 if(event>=EVENT_OBJECT_CREATE&&!tracked_.contains(h)&&!IsWindowVisible(h))return;
 if(event==EVENT_OBJECT_NAMECHANGE&&!tracked_.contains(h)&&h!=GetForegroundWindow())return;
 // Keep every affected HWND even when the refresh message is already pending.
 // Geometry-only events retain their cached Shell identity to avoid extra queries.
 if(event!=EVENT_OBJECT_LOCATIONCHANGE&&event!=EVENT_OBJECT_CLOAKED&&event!=EVENT_OBJECT_UNCLOAKED&&event!=EVENT_SYSTEM_MOVESIZEEND&&event!=EVENT_SYSTEM_MOVESIZESTART)invalidated_.insert(h);
 if(gate_.request())PostMessageW(receiver_,RefreshMessage,event,reinterpret_cast<LPARAM>(h));
}
bool WindowEvents::start(HWND h){stop();gate_.setPaused(false);receiver_=h;auto add=[&](DWORD lo,DWORD hi){auto hook=SetWinEventHook(lo,hi,nullptr,callback,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);if(hook)hooks_.push_back(hook);};add(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND);add(EVENT_SYSTEM_DESKTOPSWITCH,EVENT_SYSTEM_DESKTOPSWITCH);add(EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND);add(EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZEEND);add(EVENT_OBJECT_CREATE,EVENT_OBJECT_HIDE);add(EVENT_OBJECT_CLOAKED,EVENT_OBJECT_UNCLOAKED);add(EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE);add(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE);return !hooks_.empty();}
void WindowEvents::stop(){for(auto h:hooks_)UnhookWinEvent(h);hooks_.clear();receiver_=nullptr;tracked_.clear();invalidated_.clear();moving_.clear();moveEnds_.clear();lifecycle_.clear();gate_.setPaused(true);}

// A dedicated transparent window owns the full edge reservation. The visible dock
// may then be narrower and rounded without violating ABM_SETPOS geometry.
LRESULT CALLBACK Appbar::procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
 auto* self=reinterpret_cast<Appbar*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
 if(message==WM_NCCREATE){
  self=static_cast<Appbar*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
  SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
 }
 if(message==WM_NCHITTEST)return HTTRANSPARENT;
 if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(message==WM_ERASEBKGND)return 1;
 if(message==WM_PAINT){ValidateRect(hwnd,nullptr);return 0;}
 if(message==AppbarMessage&&self&&self->owner_){
  if(w==ABN_POSCHANGED)PostMessageW(self->owner_,AppbarMessage,w,l);
  return 0;
 }
 if(message==WM_NCDESTROY)SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
 return DefWindowProcW(hwnd,message,w,l);
}
void Appbar::attach(HWND owner,UINT edge){
 if(owner_==owner&&registered_&&edge_==edge)return;
 detach();owner_=owner;edge_=edge;
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(nullptr);
 wc.lpszClassName=L"Aether.Reservation.v3";wc.lpfnWndProc=procedure;
 if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
 reservation_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_LAYERED,
  wc.lpszClassName,L"Aether layout reservation",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,this);
 if(!reservation_)return;
 // Fully transparent layered windows do not intercept mouse input.
 SetLayeredWindowAttributes(reservation_,0,0,LWA_ALPHA);
 APPBARDATA data{sizeof(data)};data.hWnd=reservation_;data.uCallbackMessage=AppbarMessage;
 registered_=SHAppBarMessage(ABM_NEW,&data)!=0;
 if(!registered_){DestroyWindow(reservation_);reservation_=nullptr;}
}
Rect Appbar::position(Rect screen,int thickness){
 if(!registered_||busy_)return area_;busy_=true;
 APPBARDATA data{sizeof(data)};data.hWnd=reservation_;data.uEdge=edge_;data.rc=native(screen);
 SHAppBarMessage(ABM_QUERYPOS,&data);
 switch(edge_){
 case ABE_TOP:data.rc.bottom=data.rc.top+thickness;break;
 case ABE_BOTTOM:data.rc.top=data.rc.bottom-thickness;break;
 case ABE_LEFT:data.rc.right=data.rc.left+thickness;break;
 case ABE_RIGHT:data.rc.left=data.rc.right-thickness;break;
 }
 auto proposed=rect(data.rc);
 if(proposed!=area_||pixels_!=thickness){
  SHAppBarMessage(ABM_SETPOS,&data);area_=rect(data.rc);pixels_=thickness;
  SetWindowPos(reservation_,HWND_BOTTOM,area_.left,area_.top,area_.width(),area_.height(),SWP_NOACTIVATE|SWP_SHOWWINDOW);
 }
 busy_=false;return area_;
}
void Appbar::detach(){
 if(registered_){registered_=false;APPBARDATA data{sizeof(data)};data.hWnd=reservation_;SHAppBarMessage(ABM_REMOVE,&data);}
 if(reservation_)DestroyWindow(reservation_);
 reservation_=nullptr;owner_=nullptr;area_={};pixels_=0;
}
}
