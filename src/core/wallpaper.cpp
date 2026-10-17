#include "wallpaper.hpp"
#include <algorithm>
#include <limits>
namespace aether {
namespace {
bool text(std::wstring_view value,std::size_t limit,bool empty=false){
 if((!empty&&value.empty())||value.size()>limit)return false;
 return std::none_of(value.begin(),value.end(),[](wchar_t c){return c<32;});
}
bool fit(WallpaperFit value){return static_cast<std::uint32_t>(value)<=5;}
bool bounds(Rect r){return r.left>=-1000000&&r.top>=-1000000&&r.right<=1000000&&r.bottom<=1000000&&r.left<r.right&&r.top<r.bottom;}
bool displays(const std::vector<WallpaperDisplay>& value,bool empty=false){
 if((!empty&&value.empty())||value.size()>WallpaperMonitorLimit)return false;
 for(std::size_t i=0;i<value.size();++i){
  if(!text(value[i].device,WallpaperDeviceLimit)||!bounds(value[i].bounds))return false;
  for(std::size_t j=0;j<i;++j)if(value[i].device==value[j].device||value[i].bounds==value[j].bounds)return false;
 }
 return true;
}
std::vector<WallpaperDisplay> topology(const WallpaperSnapshot& snapshot){std::vector<WallpaperDisplay> result;for(const auto& monitor:snapshot.monitors)result.push_back(monitor.display);return result;}
bool reversible(const WallpaperSnapshot& snapshot){return !snapshot.slideshow&&std::all_of(snapshot.monitors.begin(),snapshot.monitors.end(),[](const auto& monitor){return !monitor.path.empty();});}
const WallpaperMonitor* monitorFor(const WallpaperSnapshot& snapshot,const std::wstring& device){auto found=std::find_if(snapshot.monitors.begin(),snapshot.monitors.end(),[&](const auto& m){return m.display.device==device;});return found==snapshot.monitors.end()?nullptr:&*found;}
}
bool validWallpaperPath(std::wstring_view path,bool allowEmpty){
 if(path.empty())return allowEmpty;
 if(!text(path,WallpaperPathLimit))return false;
 const bool drive=path.size()>=3&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&(path[2]==L'\\'||path[2]==L'/');
 const bool unc=path.size()>4&&path[0]==L'\\'&&path[1]==L'\\'&&path[2]!=L'?'&&path[2]!=L'.'&&path.find(L'\\',2)!=std::wstring_view::npos;
 if(!drive&&!unc)return false;
 return path.find_first_of(L"<>\"|*?")==std::wstring_view::npos&&path.find(L':',drive?2:0)==std::wstring_view::npos;
}
bool validWallpaperSnapshot(const WallpaperSnapshot& snapshot){
 if(!fit(snapshot.fit)||snapshot.color>0xffffff||snapshot.monitors.empty()||snapshot.monitors.size()>WallpaperMonitorLimit)return false;
 for(const auto& monitor:snapshot.monitors)if(!validWallpaperPath(monitor.path,true))return false;
 return displays(topology(snapshot));
}
bool sameWallpaperTopology(const std::vector<WallpaperDisplay>& expected,const WallpaperSnapshot& actual){
 if(!displays(expected)||!validWallpaperSnapshot(actual)||expected.size()!=actual.monitors.size())return false;
 return std::all_of(expected.begin(),expected.end(),[&](const auto& display){auto monitor=monitorFor(actual,display.device);return monitor&&monitor->display.bounds==display.bounds;});
}
bool sameWallpaperState(const WallpaperSnapshot& a,const WallpaperSnapshot& b){
 if(!sameWallpaperTopology(topology(a),b)||a.fit!=b.fit||a.color!=b.color||a.slideshow!=b.slideshow)return false;
 return std::all_of(a.monitors.begin(),a.monitors.end(),[&](const auto& monitor){auto other=monitorFor(b,monitor.display.device);return other&&monitor.path==other->path;});
}
std::optional<std::vector<WallpaperDisplay>> mapWallpaperDisplays(const WallpaperSnapshot& snapshot,const std::vector<Rect>& physicalBounds){
 if(!validWallpaperSnapshot(snapshot)||physicalBounds.empty()||physicalBounds.size()!=snapshot.monitors.size())return {};
 std::vector<WallpaperDisplay> result;
 for(const auto& rectangle:physicalBounds){
  auto match=std::find_if(snapshot.monitors.begin(),snapshot.monitors.end(),[&](const auto& monitor){return monitor.display.bounds==rectangle;});
  if(match==snapshot.monitors.end()||std::any_of(result.begin(),result.end(),[&](const auto& display){return display.device==match->display.device;}))return {};
  result.push_back(match->display);
 }
 return result;
}
bool validWallpaperRequest(const WallpaperRequest& request){
 if(static_cast<std::uint32_t>(request.kind)>2||!fit(request.fit)||!displays(request.displays,true))return false;
 if(request.kind==WallpaperKind::Query)return request.image.empty()&&request.selectedDevice.empty()&&!request.before&&!request.expected;
 if(request.displays.empty())return false;
 if(request.kind==WallpaperKind::Apply){
  if(!validWallpaperPath(request.image)||request.before||request.expected||!text(request.selectedDevice,WallpaperDeviceLimit,true))return false;
  return request.selectedDevice.empty()||std::any_of(request.displays.begin(),request.displays.end(),[&](const auto& display){return display.device==request.selectedDevice;});
 }
 return request.image.empty()&&request.selectedDevice.empty()&&request.before&&request.expected&&validWallpaperSnapshot(*request.before)&&validWallpaperSnapshot(*request.expected)
  &&sameWallpaperTopology(request.displays,*request.before)&&sameWallpaperTopology(request.displays,*request.expected)&&reversible(*request.before)&&reversible(*request.expected);
}
WallpaperResult executeWallpaper(WallpaperBackend& backend,const WallpaperRequest& request){
 WallpaperResult result;result.kind=request.kind;
 if(!validWallpaperRequest(request)){result.status=WallpaperStatus::Invalid;result.message=L"Invalid or unsupported background request.";return result;}
 auto current=backend.query();
 if(!current||!validWallpaperSnapshot(*current)){result.message=L"Windows background information is unavailable.";return result;}
 result.after=*current;
 if(request.kind==WallpaperKind::Query){result.status=WallpaperStatus::Ok;return result;}
 if(!sameWallpaperTopology(request.displays,*current)){result.status=WallpaperStatus::Stale;result.message=L"Displays changed. Refresh before applying a background.";return result;}
 if(!reversible(*current)){result.status=WallpaperStatus::Invalid;result.message=L"Slideshow or solid-color backgrounds must be changed in Windows Settings.";return result;}
 if(request.kind==WallpaperKind::Undo&&!sameWallpaperState(*current,*request.expected)){
  result.status=WallpaperStatus::Stale;result.message=L"Background changed elsewhere. Undo was skipped to preserve it.";return result;
 }
 if(!backend.restorable(request.kind==WallpaperKind::Apply?*current:*request.before)){
  result.status=WallpaperStatus::Invalid;result.message=request.kind==WallpaperKind::Apply?L"Previous background images cannot be restored safely. Use Windows Settings.":L"A previous background image is unavailable. Undo was skipped.";return result;
 }
 result.before=*current;
 auto desired=request.kind==WallpaperKind::Undo?*request.before:*current;
 if(request.kind==WallpaperKind::Apply){desired.fit=request.fit;for(auto& monitor:desired.monitors)if(request.selectedDevice.empty()||monitor.display.device==request.selectedDevice)monitor.path=request.image;}
 bool ok=true;
 if(current->fit!=desired.fit)ok=backend.position(desired.fit);
 if(ok&&current->color!=desired.color)ok=backend.background(desired.color);
 if(ok)for(const auto& monitor:desired.monitors){
  const auto* previous=monitorFor(*current,monitor.display.device);
  if(previous&&previous->path!=monitor.path&&!backend.image(monitor.display.device,monitor.path)){ok=false;break;}
 }
 auto observed=backend.query();
 if(!observed||!validWallpaperSnapshot(*observed)){
  result.after.reset();result.status=WallpaperStatus::Uncertain;result.message=L"Background outcome is unknown. Refresh before another action.";return result;
 }
 result.after=*observed;
 if(ok&&sameWallpaperState(desired,*observed)){result.status=WallpaperStatus::Ok;result.message=request.kind==WallpaperKind::Undo?L"Previous background restored.":L"Background applied.";return result;}
 result.status=sameWallpaperState(*current,*observed)?WallpaperStatus::Failed:WallpaperStatus::Partial;
 result.message=result.status==WallpaperStatus::Partial?L"Only part of the change completed. Review the current background before another action.":L"Windows did not apply the background.";
 return result;
}
}
