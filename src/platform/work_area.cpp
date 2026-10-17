#include "work_area.hpp"
namespace aether::win {
namespace {
class PhysicalDpiScope {
 DPI_AWARENESS_CONTEXT previous_=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
public:
 ~PhysicalDpiScope(){if(previous_)SetThreadDpiAwarenessContext(previous_);}
 explicit operator bool()const noexcept{return previous_!=nullptr;}
};
class NativeWorkAreaBackend final:public WorkAreaBackend {
public:
 bool captureCurrent(WorkAreaSnapshot& result) noexcept override {
  PhysicalDpiScope dpi;if(!dpi)return false;
  struct Capture {WorkAreaSnapshot snapshot;bool failed=false;};Capture capture;
  const bool complete=EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM value)->BOOL{
   auto& capture=*reinterpret_cast<Capture*>(value);
   if(capture.snapshot.count==WorkAreaCapacity){capture.failed=true;return FALSE;}
   MONITORINFOEXW info{};info.cbSize=sizeof(info);
   if(!GetMonitorInfoW(monitor,&info)){capture.failed=true;return FALSE;}
   auto& entry=capture.snapshot.entries[capture.snapshot.count++];
   std::copy_n(info.szDevice,CCHDEVICENAME,entry.device);entry.bounds=info.rcMonitor;entry.work=info.rcWork;return TRUE;
  },reinterpret_cast<LPARAM>(&capture))!=FALSE;
  if(!complete||capture.failed||!workAreaSnapshotValid(capture.snapshot))return false;
  result=capture.snapshot;return true;
 }
 bool requestCaptured(const WorkAreaEntry& original) noexcept override {
  PhysicalDpiScope dpi;if(!dpi)return false;
  RECT desired=original.work;const auto monitor=MonitorFromRect(&desired,MONITOR_DEFAULTTONULL);
  MONITORINFOEXW current{};current.cbSize=sizeof(current);
  if(!monitor||!GetMonitorInfoW(monitor,&current)||wcscmp(current.szDevice,original.device)
   ||!EqualRect(&current.rcMonitor,&original.bounds))return false;
  // No profile update, broadcast, monitor guess, or replacement RECT.
  return SystemParametersInfoW(SPI_SETWORKAREA,0,&desired,0)!=FALSE;
 }
};
}
bool workAreaSnapshotValid(const WorkAreaSnapshot& snapshot) noexcept {
 if(!snapshot.count||snapshot.count>WorkAreaCapacity)return false;
 for(DWORD i=0;i<snapshot.count;++i){const auto& entry=snapshot.entries[i];
  if(!entry.device[0]||!std::wmemchr(entry.device,L'\0',CCHDEVICENAME)
   ||entry.bounds.left>=entry.bounds.right||entry.bounds.top>=entry.bounds.bottom
   ||entry.work.left<entry.bounds.left||entry.work.top<entry.bounds.top
   ||entry.work.right>entry.bounds.right||entry.work.bottom>entry.bounds.bottom
   ||entry.work.left>=entry.work.right||entry.work.top>=entry.work.bottom)return false;
  for(DWORD j=0;j<i;++j)if(wcscmp(entry.device,snapshot.entries[j].device)==0)return false;
 }return true;
}
bool captureWorkAreas(WorkAreaSnapshot& result,WorkAreaBackend& backend) noexcept {
 WorkAreaSnapshot captured;if(!backend.captureCurrent(captured)||!workAreaSnapshotValid(captured))return false;
 result=captured;return true;
}
bool captureWorkAreas(WorkAreaSnapshot& result) noexcept {NativeWorkAreaBackend backend;return captureWorkAreas(result,backend);}
WorkAreaResult compareWorkAreas(const WorkAreaSnapshot& original,const WorkAreaSnapshot& current) noexcept {
 if(!workAreaSnapshotValid(original)||!workAreaSnapshotValid(current))return WorkAreaResult::Invalid;
 if(original.count!=current.count)return WorkAreaResult::TopologyChanged;
 bool matchingWork=true;
 for(DWORD i=0;i<original.count;++i){const auto& before=original.entries[i];const WorkAreaEntry* found=nullptr;
  for(DWORD j=0;j<current.count;++j)if(wcscmp(before.device,current.entries[j].device)==0){found=&current.entries[j];break;}
  if(!found||!EqualRect(&before.bounds,&found->bounds))return WorkAreaResult::TopologyChanged;
  if(!EqualRect(&before.work,&found->work))matchingWork=false;
 }return matchingWork?WorkAreaResult::Complete:WorkAreaResult::Pending;
}
WorkAreaResult restoreWorkAreas(const WorkAreaSnapshot& original,WorkAreaBackend& backend) noexcept {
 if(!workAreaSnapshotValid(original))return WorkAreaResult::Invalid;
 WorkAreaSnapshot current;
 for(DWORD i=0;i<original.count;++i){
  // A partial success is retained, but never authorizes writing another
  // monitor after a topology/device change. Read the entire topology per write.
  if(!captureWorkAreas(current,backend))return WorkAreaResult::Unavailable;
  const auto observed=compareWorkAreas(original,current);
  if(observed==WorkAreaResult::TopologyChanged||observed==WorkAreaResult::Invalid)return observed;
  if(observed==WorkAreaResult::Complete)return observed;
  const auto& before=original.entries[i];const WorkAreaEntry* target=nullptr;
  for(DWORD j=0;j<current.count;++j)if(wcscmp(before.device,current.entries[j].device)==0){target=&current.entries[j];break;}
  if(!target||!EqualRect(&before.bounds,&target->bounds))return WorkAreaResult::TopologyChanged;
  if(!EqualRect(&before.work,&target->work)&&!backend.requestCaptured(before))return WorkAreaResult::Pending;
 }
 if(!captureWorkAreas(current,backend))return WorkAreaResult::Unavailable;
 return compareWorkAreas(original,current);
}
WorkAreaResult restoreWorkAreas(const WorkAreaSnapshot& original) noexcept {NativeWorkAreaBackend backend;return restoreWorkAreas(original,backend);}
}
