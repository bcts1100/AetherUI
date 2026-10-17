#include "tiling.hpp"
#include <limits>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
namespace aether::win {
namespace {
using Token=std::pair<HWND,DWORD>;
Rect frame(HWND hwnd){RECT bounds{};if(FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds))))GetWindowRect(hwnd,&bounds);return rect(bounds);}
bool closeRect(Rect a,Rect b){return std::abs(static_cast<long long>(a.left)-b.left)<=2&&std::abs(static_cast<long long>(a.top)-b.top)<=2&&std::abs(static_cast<long long>(a.right)-b.right)<=2&&std::abs(static_cast<long long>(a.bottom)-b.bottom)<=2;}
bool shellWindow(HWND hwnd){wchar_t cls[128]{};GetClassNameW(hwnd,cls,128);return wcscmp(cls,L"Shell_TrayWnd")==0||wcscmp(cls,L"Shell_SecondaryTrayWnd")==0||wcscmp(cls,L"Progman")==0||wcscmp(cls,L"WorkerW")==0;}
struct Minimum {int width=1,height=1;};
std::optional<Minimum> minimum(HWND hwnd,int dpi){
 MINMAXINFO info{};info.ptMinTrackSize={GetSystemMetricsForDpi(SM_CXMINTRACK,static_cast<UINT>(std::clamp(dpi,48,480))),GetSystemMetricsForDpi(SM_CYMINTRACK,static_cast<UINT>(std::clamp(dpi,48,480)))};DWORD_PTR result=0;
 if(!SendMessageTimeoutW(hwnd,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&info),SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result))return {};
 RECT outer{};if(!GetWindowRect(hwnd,&outer))return {};auto visible=frame(hwnd);int width=std::max(1,static_cast<int>(info.ptMinTrackSize.x)-(static_cast<int>(outer.right-outer.left)-visible.width())),height=std::max(1,static_cast<int>(info.ptMinTrackSize.y)-(static_cast<int>(outer.bottom-outer.top)-visible.height()));
 if(width>1000000||height>1000000)return {};return Minimum{width,height};
}
bool applyFrame(HWND hwnd,DWORD pid,Rect target){
 if(!windowAlive(hwnd,pid)||IsIconic(hwnd)||!IsWindowEnabled(hwnd))return false;RECT outer{};if(!GetWindowRect(hwnd,&outer))return false;auto visible=frame(hwnd);
 auto x=static_cast<long long>(target.left)+outer.left-visible.left,y=static_cast<long long>(target.top)+outer.top-visible.top;
 auto width=static_cast<long long>(target.width())+(outer.right-outer.left)-visible.width(),height=static_cast<long long>(target.height())+(outer.bottom-outer.top)-visible.height();
 if(x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()||y<std::numeric_limits<int>::min()||y>std::numeric_limits<int>::max()||width<1||height<1||width>1000000||height>1000000)return false;
 return SetWindowPos(hwnd,nullptr,static_cast<int>(x),static_cast<int>(y),static_cast<int>(width),static_cast<int>(height),SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_ASYNCWINDOWPOS)!=FALSE;
}
std::optional<std::size_t> onMonitor(HWND hwnd,const std::vector<Monitor>& monitors){auto handle=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);for(std::size_t i=0;i<monitors.size();++i)if(monitors[i].handle==handle)return i;return {};}
std::string planSignature(const Monitor& monitor,const SnapLayout& layout,const std::vector<std::pair<Token,std::uint64_t>>& members){std::ostringstream s;s<<monitor.work.left<<','<<monitor.work.top<<','<<monitor.work.right<<','<<monitor.work.bottom<<','<<monitor.dpi<<':'<<layout.id<<':'<<layout.gapDips;for(auto r:layout.zones)s<<'/'<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom;for(const auto& [token,order]:members)s<<'/'<<reinterpret_cast<std::uintptr_t>(token.first)<<','<<token.second<<','<<order;return s.str();}
}
struct TilingService::Impl {
 struct Entry {Rect last{},expected{};bool hasExpected=false,released=false;ULONGLONG deadline=0;std::uint64_t order=0;};
 bool fixturesOnly=false;std::map<HWND,DWORD> fixtures;std::map<Token,Entry> entries;std::map<std::wstring,std::string> signatures;std::uint64_t order=0;
 bool eligible(HWND hwnd,DWORD pid,bool explicitCommand=false) const{
  if(!windowAlive(hwnd,pid))return false;if(fixturesOnly){auto allowed=fixtures.find(hwnd);if(allowed==fixtures.end()||allowed->second!=pid)return false;}else if(pid==GetCurrentProcessId())return false;
  if(!IsWindowVisible(hwnd)||IsIconic(hwnd)||(!explicitCommand&&IsZoomed(hwnd))||!IsWindowEnabled(hwnd)||shellWindow(hwnd))return false;
  auto style=GetWindowLongPtrW(hwnd,GWL_STYLE),ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);if(!(style&WS_THICKFRAME)||(ex&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE))||GetWindow(hwnd,GW_OWNER))return false;
  BOOL cloaked=FALSE;if(SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked)return false;
  if(!IsZoomed(hwnd)){MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))return false;auto r=frame(hwnd);if(r.left<=monitor.rcMonitor.left&&r.top<=monitor.rcMonitor.top&&r.right>=monitor.rcMonitor.right&&r.bottom>=monitor.rcMonitor.bottom)return false;}
  return true;
 }
};
TilingService::TilingService(bool fixturesOnly):impl_(std::make_unique<Impl>()){impl_->fixturesOnly=fixturesOnly;}
TilingService::~TilingService()=default;
void TilingService::allowFixture(HWND hwnd,DWORD pid){if(impl_->fixturesOnly&&hwnd&&pid&&impl_->fixtures.size()<256)impl_->fixtures[hwnd]=pid;}
void TilingService::observeUserMove(HWND hwnd){for(auto& [token,entry]:impl_->entries)if(token.first==hwnd&&!entry.released){entry.released=true;++stats_.released;}}
void TilingService::forgetWindow(HWND hwnd){std::erase_if(impl_->entries,[&](const auto& item){return item.first.first==hwnd;});}
void TilingService::reset(){impl_->entries.clear();impl_->signatures.clear();}
void TilingService::update(const Config& config,const std::vector<Monitor>& monitors,const std::vector<Window>& windows,bool suppressed,const std::wstring& retileDisplay){
 if(suppressed){++stats_.suppressed;return;}if(!validTilingConfig(config)){++stats_.suppressed;return;}
 const auto now=GetTickCount64(),started=now;std::set<Token> live;for(const auto& window:windows)live.insert({window.hwnd,window.pid});std::erase_if(impl_->entries,[&](const auto& item){return !live.contains(item.first);});
 std::set<std::wstring> fullscreen;for(const auto& window:windows)if(!window.minimized&&window.fullscreen)for(const auto& monitor:monitors)if(window.display==monitor.name||(window.bounds.left<=monitor.bounds.left&&window.bounds.top<=monitor.bounds.top&&window.bounds.right>=monitor.bounds.right&&window.bounds.bottom>=monitor.bounds.bottom))fullscreen.insert(monitor.name);
 std::set<std::wstring> modalDisplays;for(const auto& window:windows)if(!window.minimized&&windowAlive(window.hwnd,window.pid)&&IsWindowVisible(window.hwnd)&&!IsWindowEnabled(window.hwnd))modalDisplays.insert(window.display);
 std::set<std::wstring> known;for(const auto& monitor:monitors)known.insert(monitor.name);std::erase_if(impl_->signatures,[&](const auto& item){return !known.contains(item.first);});
 for(const auto& monitor:monitors){
  auto options=config.tilingForDisplay(utf8(monitor.name));bool explicitRetile=!retileDisplay.empty()&&retileDisplay==monitor.name;
  if(!options.enabled&&!explicitRetile){impl_->signatures.erase(monitor.name);continue;}if(fullscreen.contains(monitor.name)||modalDisplays.contains(monitor.name)){++stats_.suppressed;continue;}auto layout=findSnapLayout(config,options.layoutId);if(!layout)continue;
  std::vector<std::pair<Token,std::uint64_t>> members;
  for(const auto& window:windows){if(window.display!=monitor.name||window.minimized||window.fullscreen||!impl_->eligible(window.hwnd,window.pid))continue;Token token{window.hwnd,window.pid};auto found=impl_->entries.find(token);
   if(found==impl_->entries.end()){if(impl_->entries.size()>=2048)continue;found=impl_->entries.emplace(token,Impl::Entry{window.bounds,{},false,false,0,++impl_->order}).first;}
   auto& entry=found->second;if(explicitRetile){entry.released=false;entry.hasExpected=false;entry.last=window.bounds;}
   else if(!entry.released){bool changed=entry.hasExpected?!closeRect(window.bounds,entry.expected):!closeRect(window.bounds,entry.last);if(changed&&(!entry.hasExpected||now>=entry.deadline)){entry.released=true;++stats_.released;}}
   if(entry.hasExpected&&closeRect(window.bounds,entry.expected))entry.deadline=0;entry.last=window.bounds;
   if(!entry.released)members.push_back({token,entry.order});
  }
  std::sort(members.begin(),members.end(),[](const auto& a,const auto& b){return a.second<b.second;});if(members.size()>TilingWindowLimit)members.resize(TilingWindowLimit);if(!layout->zones.empty()&&members.size()>layout->zones.size())members.resize(layout->zones.size());
  auto signature=planSignature(monitor,*layout,members);auto previous=impl_->signatures.find(monitor.name);if(!explicitRetile&&previous!=impl_->signatures.end()&&previous->second==signature)continue;impl_->signatures[monitor.name]=signature;if(members.empty())continue;++stats_.plans;
  auto rects=tileRects(monitor.work,members.size(),*layout,monitor.dpi);if(rects.size()!=members.size()){++stats_.failures;continue;}
  for(std::size_t i=0;i<members.size();++i){auto token=members[i].first;auto& entry=impl_->entries.at(token);if(!impl_->eligible(token.first,token.second))continue;if(closeRect(frame(token.first),rects[i])){entry.expected=rects[i];entry.hasExpected=true;entry.deadline=0;continue;}
   if(GetTickCount64()-started>=160){entry.released=true;++stats_.released;++stats_.failures;continue;}
   auto limits=minimum(token.first,monitor.dpi);if(!limits||rects[i].width()<limits->width||rects[i].height()<limits->height||!applyFrame(token.first,token.second,rects[i])){entry.released=true;++stats_.released;++stats_.failures;continue;}
   entry.expected=rects[i];entry.hasExpected=true;entry.deadline=GetTickCount64()+1000;++stats_.moves;
  }
 }
}
bool TilingService::snapWindow(const Window& window,const Config& config,const std::vector<Monitor>& monitors,int zone,bool custom){
 HWND hwnd=window.hwnd;DWORD pid=window.pid;if(!impl_->eligible(hwnd,pid,true))return false;auto index=onMonitor(hwnd,monitors);if(!index)return false;const auto& monitor=monitors[*index];
 if(custom){auto layout=findSnapLayout(config,config.tilingForDisplay(utf8(monitor.name)).layoutId);if(zone<0||zone>=9||!layout||layout->zones.empty()||static_cast<std::size_t>(zone)>=layout->zones.size())return false;}else if(zone<0||zone>6)return false;
 // Explicit user command restores maximized windows through a bounded send.
 // Auto eligibility still excludes them; minimized windows are never restored.
 if(IsZoomed(hwnd)){DWORD_PTR result=0;if(!SendMessageTimeoutW(hwnd,WM_SYSCOMMAND,SC_RESTORE,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result)||IsZoomed(hwnd)||!windowAlive(hwnd,pid))return false;}
 auto limits=minimum(hwnd,monitor.dpi);if(!limits)return false;std::optional<Rect> target;
 if(custom){auto layout=findSnapLayout(config,config.tilingForDisplay(utf8(monitor.name)).layoutId);target=zoneRect(monitor.work,layout->zones[static_cast<std::size_t>(zone)],layout->gapDips,monitor.dpi,limits->width,limits->height);}
 else{const Rect zones[]={{0,0,5000,10000},{5000,0,10000,10000},{0,0,5000,5000},{5000,0,10000,5000},{0,5000,5000,10000},{5000,5000,10000,10000},{0,0,10000,10000}};target=zoneRect(monitor.work,zones[zone],8,monitor.dpi,limits->width,limits->height);}
 if(!target||!applyFrame(hwnd,pid,*target)){++stats_.failures;return false;}observeUserMove(hwnd);++stats_.moves;return true;
}
bool TilingService::snapActive(const Config& config,const std::vector<Monitor>& monitors,int zone,bool custom){Window window;window.hwnd=GetForegroundWindow();GetWindowThreadProcessId(window.hwnd,&window.pid);return snapWindow(window,config,monitors,zone,custom);}
bool TilingService::moveWindow(const Window& window,const std::vector<Monitor>& monitors,MoveDirection direction){
 HWND hwnd=window.hwnd;DWORD pid=window.pid;if(!impl_->eligible(hwnd,pid,true))return false;auto source=onMonitor(hwnd,monitors);if(!source)return false;std::vector<Rect> areas;for(const auto& monitor:monitors)areas.push_back(monitor.bounds);auto target=adjacentMonitorIndex(areas,*source,direction);if(!target)return false;
 // Resolve a real destination before touching maximize state.
 if(IsZoomed(hwnd)){DWORD_PTR result=0;if(!SendMessageTimeoutW(hwnd,WM_SYSCOMMAND,SC_RESTORE,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result)||IsZoomed(hwnd)||!windowAlive(hwnd,pid))return false;}
 auto limits=minimum(hwnd,monitors[*target].dpi);if(!limits)return false;
 auto moved=transferRect(monitors[*source].work,monitors[*target].work,frame(hwnd),monitors[*source].dpi,monitors[*target].dpi,limits->width,limits->height);if(!moved||!applyFrame(hwnd,pid,*moved)){++stats_.failures;return false;}observeUserMove(hwnd);++stats_.moves;return true;
}
bool TilingService::moveActive(const std::vector<Monitor>& monitors,MoveDirection direction){Window window;window.hwnd=GetForegroundWindow();GetWindowThreadProcessId(window.hwnd,&window.pid);return moveWindow(window,monitors,direction);}
}